/*
    LXLoader - Loads LX exe files or DLLs for execution or to extract information from.
    Copyright (C) 2007  Sven Rosén (aka Viking)
    Copyright (C) 2020  Valery Sedletski (aka valerius)

    Page unpacking code

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
    Or see <http://www.gnu.org/licenses/>
*/

#include <os2.h>
#include <exe386.h>

#include <os3/io.h>

#include <string.h>

#define PAGESIZE 0x1000

int unpack_page(unsigned char *dst, unsigned char *src, struct o32_map *map);
int unpack_page_exepack1(unsigned char *dst, unsigned char *src, int size);
int unpack_page_exepack2(unsigned char *dst, unsigned char *src, int size);
int unpack_page_exepack3(unsigned char *dst, unsigned char *src, int size);
unsigned short *memsetw(unsigned short *dst, unsigned short data, int n);
unsigned int *memsetd(unsigned int *dst, unsigned int data, int n);
unsigned char *linmove(unsigned char *dst, unsigned char *src, int size);
void update_freq(int a, int b);
void update_model(long code);
void initialize(void);


int unpack_page(unsigned char *dst, unsigned char *src, struct o32_map *map)
{
    int ret = 0;

    memset(dst, 0, PAGESIZE);

    switch (map->o32_pageflags)
    {
        case VALID:
            // copied as is
            io_log("page is non-packed\n");
            memcpy(dst, src, map->o32_pagesize);
            ret = map->o32_pagesize;
            break;

        case ITERDATA:
            io_log("page packed with exepack:1\n");
            ret = unpack_page_exepack1(dst, src, map->o32_pagesize);
            break;

        case ITERDATA2:
            io_log("page packed with exepack:2\n");
            ret = unpack_page_exepack2(dst, src, map->o32_pagesize);
            break;

        case ITERDATA3:
            io_log("page packed with exepack:3\n");
            ret = unpack_page_exepack3(dst, src, map->o32_pagesize);
            break;

        case RANGE:
            // unsupported for now
            io_log("page range-packed (unsupported)\n");
            ret = PAGESIZE;
            break;

        case ZEROED:
        case INVALID:
            // zero-filled
            io_log("page zeroed/invalid\n");
            ret = PAGESIZE;
            break;

        default:
            io_log("Unsupported packing type: %u\n", map->o32_pageflags);
            ret = 0;
            break;
    }

    return ret;
}


/*  exepack:1, run-length encoding. The algorithm was borrowed from QSINIT sources. */
int unpack_page_exepack1(unsigned char *dst, unsigned char *src, int size)
{
    struct LX_Iter *iter = (struct LX_Iter *)src;
    unsigned char *start = dst;
    unsigned int save = *(unsigned int *)(src + size);
    *(unsigned int *)(src + size) = 0;

    while (iter->LX_nIter)
    {
        switch (iter->LX_nBytes)
        {
            case 1:
                memset(dst, iter->LX_Iterdata, iter->LX_nIter);
                dst += iter->LX_nIter;
                break;

            case 2:
                memsetw((unsigned short *)dst, *(unsigned short *)&iter->LX_Iterdata, iter->LX_nIter);
                dst += iter->LX_nIter << 1;
                break;

            case 4:
                memsetd((unsigned int *)dst, *(unsigned int *)&iter->LX_Iterdata, iter->LX_nIter);
                dst += iter->LX_nIter << 2;
                break;

            default:
                {
                    int i;

                    for (i = 0; i < iter->LX_nIter; i++)
                    {
                        memcpy(dst, &iter->LX_Iterdata, iter->LX_nBytes);
                        dst += iter->LX_nBytes;
                    }
                }
        }

        iter = (struct LX_Iter *)((char *)iter + 4 + iter->LX_nBytes);
    }

    *(unsigned int *)(src + size) = save;
    return dst - start;
}


/*  exepack:2, a kind of Lempel-Ziv. The idea is borrowed in lxlite (translated
 *  from Pascal to C) and kLdr (which source code describes the idea of algorithm
 *  in comments). Both were very helpful.
 */
int unpack_page_exepack2(unsigned char *dst, unsigned char *src, int size)
{
    unsigned char cb1, cb2;
    int  srcoff, dstoff, boff;

    srcoff = dstoff = 0;

    do
    {
        if (srcoff + 1 > size)
            break;

        cb1 = src[srcoff];

        switch (cb1 & 3) // encoding type
        {
            case 0:
                if (cb1 == 0)
                {
                   /*  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 21
                    *  |  |  |              |  |                    | |                     |
                    *  type   -----zero-----    ---------cb1--------   --char to multiply---
                    *
                    *  When the first byte is zero, this means, the following two bytes describe
                    *  the count of bytes, and the byte to copy, respectively. If the two first
                    *  bytes are zeroes, this means the end of data, so we need to stop here.
                    */
                    if (srcoff + 2 <= size)
                    {
                        if (src[srcoff + 1] == 0)
                        {
                            srcoff += 2;
                            return dstoff;
                        }
                        else
                        {
                            if (srcoff + 3 <= size &&
                                dstoff + src[srcoff + 1] <= PAGESIZE)
                            {
                                memset(&dst[dstoff], src[srcoff + 2], src[srcoff + 1]);
                                srcoff += 3;
                                dstoff += src[srcoff - 2];
                            }
                        }
                    }
                }
                else
                {
                   /*  0  1  2  3  4  5  6  7 <cb1 bytes of data>
                    *  |  |  |              |
                    *  type   ------cb1-----
                    *
                    *  bits 2..7 contain (if not zeroes) the count of bytes of uncompressed
                    *  data started at the 2nd byte.
                    */
                    if (srcoff + (cb1 >> 2) + 1 <= size &&
                        dstoff + (cb1 >> 2) <= PAGESIZE)
                    {
                        linmove(&dst[dstoff], &src[srcoff + 1], cb1 >> 2);
                        dstoff += cb1 >> 2;
                        srcoff += (cb1 >> 2) + 1;
                    }
                }
                break;

            case 1:
               /*  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 <cb1 bytes of data>
                *  |  |  |  |  |     |  |                       |
                *  type   cb1   cb2-3    ---------boff----------
                *
                *  bits 2..3 contain number of data started after the 2nd byte. Bits
                *  4..6 contain the number of data to copy from the expanded data after
                *  the 1st copy operation. The cb2 bytes of data are copied at offset
                *  boff after the 1st copy operation.
                */
                if (srcoff + 2 > size)
                    break;

                boff = (*(unsigned short *)&src[srcoff]) >> 7;
                cb2 = ((cb1 >> 4) & 7) + 3;
                cb1 = (cb1 >> 2) & 3;
                srcoff += 2;

                if (srcoff + cb1 <= size &&
                    dstoff + cb1 + cb2 <= PAGESIZE &&
                    dstoff + cb1 >= boff)
                {
                    linmove(&dst[dstoff], &src[srcoff], cb1);
                    dstoff += cb1;
                    srcoff += cb1;
                    linmove(&dst[dstoff], &dst[dstoff - boff], cb2);
                    dstoff += cb2;
                }
                break;

            case 2:
               /*  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
                *  |  |  |  |  |                                |
                *  type  cb1-3  -------------boff---------------
                *
                *  here we need to copy cb1 bytes at offset boff starting from the
                *  current position.
                */
                if (srcoff + 2 > size)
                    break;

                boff = (*(unsigned short *)&src[srcoff]) >> 4;
                cb1 = ((cb1 >> 2) & 3) + 3;

                if (dstoff + cb1 <= PAGESIZE &&
                    dstoff >= boff)
                {
                    linmove(&dst[dstoff], &dst[dstoff - boff], cb1);
                    dstoff += cb1;
                    srcoff += 2;
                }
                break;

            case 3:
               /*  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 <cb1 bytes of data>
                *  |  |  |        |  |              | |                                 |
                *  type   ---cb1--    ------cb2-----   -------------boff---------------
                *
                *  first, we need to copy cb1 bytes of data starting after 3rd byte.
                *  second, we need to copy cb2 bytes boff bytes earlier than the current
                *  position after the 1st copy operation.
                */
                if (srcoff + 3 > size)
                    break;

                cb2 = ((*(unsigned short *)&src[srcoff]) >> 6) & 0x3f;
                cb1 = (src[srcoff] >> 2) & 0x0f;
                boff = (*(unsigned short *)&src[srcoff + 1]) >> 4;
                srcoff += 3;

                if (srcoff + cb1 <= size &&
                    dstoff + cb1 + cb2 <= PAGESIZE &&
                    dstoff + cb1 >= boff)
                {
                    linmove(&dst[dstoff], &src[srcoff], cb1);
                    dstoff += cb1;
                    srcoff += cb1;
                    linmove(&dst[dstoff], &dst[dstoff - boff], cb2);
                    dstoff += cb2;
                }
                break;
        }

    } while (dstoff < PAGESIZE);

    return dstoff;
}

#define MAXFREQ    2000
#define MINCOPY    3
#define MAXCOPY    66
#define COPYRANGES 7
#define CODESPERRANGE (MAXCOPY - MINCOPY + 1)

#define TERMINATE  256
#define FIRSTCODE  257
#define MAXCHAR    (FIRSTCODE + COPYRANGES * CODESPERRANGE - 1)
#define SUCCMAX    (MAXCHAR + 1)
#define TWICEMAX   (2 * MAXCHAR + 1)
#define ROOT       1
#define MAXSIZE    32768

long leftc[MAXCHAR + 1], rightc[MAXCHAR + 1];
long parent[TWICEMAX + 1], freq[TWICEMAX + 1];
unsigned char *curbuf;
long input_bit_count;
long input_bit_buffer;
unsigned char buffer[MAXSIZE + 1];
int  copybits[COPYRANGES] = {4, 6, 8, 10, 12, 14, 14}; /* distance bits */
int  copymin[COPYRANGES]  = {0, 16, 80, 320, 1024, 4096, 16384};
int  copymax[COPYRANGES]  = {15, 79, 319, 1023, 4095, 16383, 32768 - MAXCOPY};

/*  exepack:3, new packing method introduced by OS/4. The source code is borrowed
 *  from _dixie_'s code in lxlite (translated from Pascal to C).
 */
int unpack_page_exepack3(unsigned char *dst, unsigned char *src, int size)
{
    long i, j, k, t;
    long dist, len, index;
    long n, c;

    input_bit_count = input_bit_buffer = n = 0;
    curbuf = dst;
    initialize();

    goto start;

    while (c != TERMINATE)
    {
        if (c < 256)
        {
            /* single literal character */
            *curbuf++ = c;

            buffer[n++] = c;

            if (n == MAXSIZE)
                n = 0;
        }
        else
        {
            /* string copy length/distance codes */
            t = c - FIRSTCODE;
            index = t / CODESPERRANGE;
            len = t + MINCOPY - index * CODESPERRANGE;

            dist = 0;

            for (i = 0; i < copybits[index]; i++)
            {
                if (input_bit_count == 0)
                {
                    input_bit_buffer = *(long *)src;
                    src += 4;
                    input_bit_count = 31;
                }
                else
                    input_bit_count--;

                if (input_bit_buffer < 0)
                    dist |= (1 << i);

                input_bit_buffer <<= 1;
            }

            dist += len + copymin[index];
            j = n;
            k = n - dist;

            if (k < 0)
                k += MAXSIZE;

            for (i = 0; i < len; i++)
            {
                *curbuf++ = buffer[k];
                buffer[j] = buffer[k];

                j++; if (j == MAXSIZE) j = 0;
                k++; if (k == MAXSIZE) k = 0;
            }

            n += len; if (n >= MAXSIZE) n -= MAXSIZE;
        }
start:
        c = 1;

        do
        {
            if (input_bit_count == 0)
            {
                input_bit_buffer = *(long *)src;
                src += 4;
                input_bit_count = 31;
            }
            else
                input_bit_count--;

            if (input_bit_buffer < 0)
                c = rightc[c];
            else
                c = leftc[c];

            input_bit_buffer <<= 1;

        } while (c <= MAXCHAR);

        c -= SUCCMAX;
        update_model(c);
    }

    return curbuf - dst;
}

void initialize(void)
{
    long i;

    // Initialize Huffman frequency tree
    for (i = 2; i <= TWICEMAX; i++)
    {
        parent[i] = i >> 1;
        freq[i] = 1;
    }

    for (i = 1; i <= MAXCHAR; i++)
    {
        leftc[i] = i << 1;
        rightc[i] = (i << 1) + 1;
    }
}

/* update frequency counts from leaf to root */
void update_freq(int a, int b)
{
    do
    {
        freq[parent[a]] = freq[a] + freq[b];
        a = parent[a];

        if (a != ROOT)
        {
            if (leftc[parent[a]] == a)
                b = rightc[parent[a]];
            else
                b = leftc[parent[a]];
        }
    } while (a != ROOT);

    /* Periodically scale frequences down by half to avoid overflow.
       This also provides some local adaption abd better compression */
    if (freq[ROOT] == MAXFREQ)
    {
        for (a = 1; a <= TWICEMAX; a++)
        {
            freq[a] >>= 1;
        }
    }
}

/* Update Huffman model for each character code */
void update_model(long code)
{
    long a, b, c;
    long ua, uua;

    a = code + SUCCMAX;
    (freq[a])++;

    if (parent[a] != ROOT)
    {
        ua = parent[a];

        if (leftc[ua] == a)
            update_freq(a, rightc[ua]);
        else
            update_freq(a, leftc[ua]);

        do
        {
            uua = parent[ua];

            if (leftc[uua] == ua)
                b = rightc[uua];
            else
                b = leftc[uua];

            /* if high freq is lower in tree, swap nodes */
            if (freq[a] > freq[b])
            {
                parent[b] = ua;
                parent[a] = uua;

                if (leftc[uua] == ua)
                    rightc[uua] = a;
                else
                    leftc[uua] = a;

                if (leftc[ua] == a)
                {
                    leftc[ua] = b;
                    c = rightc[ua];
                }
                else
                {
                    rightc[ua] = b;
                    c = leftc[ua];
                }

                a = b;
                update_freq(b, c);
            }

            a = parent[a];
            ua = parent[a];

        } while (ua != ROOT);
    }
}

unsigned short *memsetw(unsigned short *dst, unsigned short data, int n)
{
    int i;

    for (i = 0; i < n; i++)
    {
        dst[i] = data;
    }

    return dst;
}

unsigned int *memsetd(unsigned int *dst, unsigned int data, int n)
{
    int i;

    for (i = 0; i < n; i++)
    {
        dst[i] = data;
    }

    return dst;
}

unsigned char *linmove(unsigned char *dst, unsigned char *src, int size)
{
    int i;

    for (i = 0; i < size; i++)
    {
        dst[i] = src[i];
    }

    return dst;
}

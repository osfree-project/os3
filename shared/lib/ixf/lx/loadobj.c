/*
    LXLoader - Loads LX exe files or DLLs for execution or to extract information from.
    Copyright (C) 2007  Sven Rosén (aka Viking)

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

/* OS/2 API defs */
#define  INCL_DOS
#define  INCL_DOSEXCEPTIONS
#include <os2.h>

/* osFree OS/2 personality internal */
#include <os3/io.h>
#include <os3/dataspace.h>
#include <os3/cfgparser.h>
#include <os3/loadobjlx.h>
#include <os3/fixuplx.h>
#include <os3/modlx.h>
#include <os3/modmgr.h>
#include <os3/ixfmgr.h>
#include <os3/native_dynlink.h>

/* libc includes */
#include <stdlib.h>
#include <string.h>

//extern struct t_mem_area os2server_root_mem_area;

void apply_internal_entry_table_fixup(struct LX_module *lx_exe_mod, struct r32_rlc *min_rlc, unsigned long int vm_start_of_page);
void apply_internal_fixup(struct LX_module *lx_exe_mod, struct r32_rlc *min_rlc, unsigned long int vm_start_of_page);
int unpack_page(unsigned char *dst, unsigned char *src, struct o32_map *map);
void apply_fixup(int type, unsigned long vm_source, unsigned long vm_target);
unsigned long flat2sel(unsigned long addr);
slist_t *lastelem (slist_t *e);

slist_t *lastelem (slist_t *e)
{
    slist_t *p, *q = 0;

    for (p = e; p; p = p->next) q = p;
    return q;
}

#if 0
/* Loads the objects for code and data, for programs. NOT used. */
int load_code_data_obj_lx(struct LX_module *lx_exe_mod, struct t_os2process *proc)
{
    struct o32_obj *kod_obj = (struct o32_obj *)get_code(lx_exe_mod);
    struct o32_obj *stack_obj = (struct o32_obj *)get_data_stack(lx_exe_mod);

    void *vm_code_obj = 0;
    void *vm_data_obj = 0;

    if ((kod_obj != 0) && (kod_obj == stack_obj))
    {   /* Data and code in same object. */
        io_log("load_code_data_obj_lx: Code and stack/data is the same object!\n");

        /* Allocate virtual memory at the address that kod_obj requires. */
        vm_code_obj = (void *) vm_alloc_obj_lx(lx_exe_mod, kod_obj);

        /* Register the allocated memory with memmgr. */
        alloc_mem_area(&proc->root_mem_area, (void *) kod_obj->o32_reserved, kod_obj->o32_size);
        //alloc_mem_area(&proc->root_mem_area, (void *) kod_obj->o32_base, kod_obj->o32_size);

        proc->code_mmap = vm_code_obj;

        if (vm_code_obj == MAP_FAILED)
        {
            io_log("Error mapping memory for(code/data)\n");
            return 0;
        }

        /* Load code object. */
        load_obj_lx(lx_exe_mod, kod_obj, vm_code_obj);
        return 1;
    }

    /* Allocate virtual memory at the address that kod_obj requires. */
    vm_code_obj = (void*) vm_alloc_obj_lx(lx_exe_mod, kod_obj);

    proc->code_mmap = vm_code_obj;

    if (vm_code_obj == MAP_FAILED)
    {
        io_log("Error mapping memory for (code)\n");
        return 0;
    }

    /* Register the allocated memory with memmgr. */
    alloc_mem_area(&proc->root_mem_area, (void *)kod_obj->o32_reserved, kod_obj->o32_size);
    //alloc_mem_area(&proc->root_mem_area, (void *) kod_obj->o32_base, kod_obj->o32_size);
    load_obj_lx(lx_exe_mod, kod_obj, vm_code_obj);

    if (stack_obj == 0)
        return 0;

    vm_data_obj = (void *)vm_alloc_obj_lx(lx_exe_mod, stack_obj);

    /* Register the allocated memory with memmgr. */

    alloc_mem_area(&proc->root_mem_area, (void *)stack_obj->o32_reserved, stack_obj->o32_size);
    //alloc_mem_area(&proc->root_mem_area, (void *)stack_obj->o32_base, stack_obj->o32_size);

    proc->stack_mmap = vm_data_obj;

    if (vm_data_obj == MAP_FAILED)
    {
        io_log("Error mapping memory for (data/stack)\n");
        return 0;
    }

    load_obj_lx(lx_exe_mod, stack_obj, vm_data_obj);

    print_o32_obj_info(*kod_obj, " Info about kod_obj ");
    print_o32_obj_info(*stack_obj, " Info about stack_obj ");

    return 1;
}
#endif

/*   Allocates virtual memory for an object at an absolute virtual address.
   Some GNU/Linux specific flags to mmap(MAP_GROWSDOWN). */
void *vm_alloc_obj_lx(IXFModule *ixfModule, struct o32_obj *lx_obj)
{
    struct LX_module *lx_exe_mod = (struct LX_module *)(ixfModule->FormatStruct);
    struct o32_obj *code_obj  = (struct o32_obj *)get_code(lx_exe_mod);
    struct o32_obj *stack_obj = (struct o32_obj *)get_data_stack(lx_exe_mod);
    //l4dm_dataspace_t ds;
    l4_os3_dataspace_t ds;
    IXFSYSDEP        *ixfSysDep;
    l4_os3_section_t *section;
    slist_t          *s;
    void             *mmap_obj = 0;
    int              align = 0;
#if 0 /*!defined(__OS2__) && !defined(__LINUX__) */
    mmap_obj = mmap((void *)(unsigned long)lx_obj->o32_base, lx_obj->o32_size,
                     PROT_WRITE | PROT_READ | PROT_EXEC  ,       /* | PROT_EXEC */
                     MAP_GROWSDOWN | MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, 0/*lx_exe_mod->fh*/,
                     0 /*lx_exe_mod->lx_head_e32_exe->e32_datapage*/);
#else
// Under OS/2 return always unique address
    #if defined(__LINUX__) || defined(__WIN32__)
        mmap_obj = malloc(lx_obj->o32_size);
        /* void * allocmem(unsigned long area, int base, int size, int flags) */
    #else
        #ifndef __OS2__
            #include <os3/allocmem.h>

            //if (lx_obj->o32_flags & OBJALIAS16)
            {
                align = 16; // align 16-bit segments to 64k boundary
            }

            // object map address in execsrv address space
            io_log("ixfModule->area=%llx, base=%lx, size=%lx\n", ixfModule->area, lx_obj->o32_base, lx_obj->o32_size);
            mmap_obj = allocmem(ixfModule->area, lx_obj->o32_base, lx_obj->o32_size,
                            PAG_COMMIT|PAG_EXECUTE|PAG_READ|PAG_WRITE, ixfModule->PIC, &ds, align);
            io_log("mmap_obj=%lx\n", mmap_obj);
            // Host-dependent part of IXFMODULE structure (data for L4 host)
            ixfSysDep = (IXFSYSDEP *)(unsigned long)(ixfModule->hdlSysDep);

            if (lx_obj == stack_obj)
            {
                ixfSysDep->stack_low = (void *)(lx_obj->o32_base + get_esp(lx_exe_mod));
                ixfSysDep->stack_high  = (void *)((unsigned long)ixfSysDep->stack_low - lx_exe_mod->lx_head_e32_exe->e32_stacksize);
                ixfModule->Stack = ixfSysDep->stack_high;

                io_log("@@@: stack_low: %x\n", ixfSysDep->stack_low);
                io_log("@@@: stack_high: %x\n", ixfSysDep->stack_high);
                io_log("@@@: o32_base: %x\n", lx_obj->o32_base);
                io_log("@@@: esp: %x\n", get_esp(lx_exe_mod));
            }

            if (lx_obj == code_obj)
            {
                ixfModule->EntryPoint = (void *)(lx_obj->o32_base + get_eip(lx_exe_mod));

                io_log("@@@: eip: %x\n", ixfModule->EntryPoint);
                io_log("@@@: o32_base: %x\n", lx_obj->o32_base);
                io_log("@@@: eip: %x\n", get_eip(lx_exe_mod));
            }

            section = (l4_os3_section_t *)malloc(sizeof(l4_os3_section_t));

            io_log("section=%x\n", section);

            s = (slist_t *)malloc(sizeof(slist_t));
            s->next = 0;
            s->section = section;

            if (! ixfSysDep->seclist)
                ixfSysDep->seclist = s;
            else
                lastelem(ixfSysDep->seclist)->next = s;

            // fill in the section info
            // object map address in client task address space (not execsrv's)
            if (! ixfModule->PIC) // for EXE files
                section->addr = (void *)(unsigned long)lx_obj->o32_base;
            else                 // for DLL files
                section->addr = (void *)mmap_obj;

            section->flags = lx_obj->o32_flags;
            io_log("### section->flags=%x\n", section->flags);

            section->type = 0;

            if (lx_obj->o32_flags & OBJREAD)
                section->type |= SECTYPE_READ;
            if (lx_obj->o32_flags & OBJWRITE)
                section->type |= SECTYPE_WRITE;
            if (lx_obj->o32_flags & OBJEXEC)
                section->type |= SECTYPE_EXECUTE;

            section->size = lx_obj->o32_size;

            section->id   = (unsigned short)ixfSysDep->secnum;
            section->ds = ds;
            ixfSysDep->secnum++;

            io_log("secnum=%lu\n", ixfSysDep->secnum);
        #else
            DosAllocMem(&mmap_obj, lx_obj->o32_size, PAG_COMMIT|PAG_EXECUTE|PAG_READ|PAG_WRITE);
        #endif
    #endif
#endif

    return mmap_obj;
}


/* Loads an object to the memory area pointed by vm_ptr_obj. */
int load_obj_lx(struct LX_module *lx_exe_mod,
                struct o32_obj *lx_obj, void *vm_ptr_obj)
{
    unsigned long tmp_code;
    void         *tmp_vm;
    void         *tmp_vm_code;
    struct o32_map *obj_pg_sta;
    unsigned long ofs_page_sta;
    unsigned long startpage = lx_obj->o32_pagemap;
    unsigned long lastpage  = lx_obj->o32_pagemap + lx_obj->o32_mapsize;
    unsigned long data_pages_offs =  get_e32_datapage(lx_exe_mod);
    unsigned long code_mmap_pos = 0;
    unsigned long page_nr = 0;
    unsigned char buf[0x4004];
    int ret = 0;

    /*struct o32_map * obj_pg_ett = get_obj_map(lx_exe_mod ,startpage);*/
    /*  Reads in all pages from kodobject to designated virtual memory. */
    for (page_nr = startpage; page_nr < lastpage; page_nr++)
    {
        obj_pg_sta = get_obj_map(lx_exe_mod, page_nr);
        ofs_page_sta = (obj_pg_sta->o32_pagedataoffset << get_e32_pageshift(lx_exe_mod))
            + data_pages_offs;

        lx_exe_mod->lx_fseek(lx_exe_mod, ofs_page_sta, SEEK_SET);

        tmp_code = code_mmap_pos;
        tmp_vm = vm_ptr_obj;
        tmp_vm_code = tmp_vm + tmp_code;

        memset(buf, 0, sizeof(buf));

        lx_exe_mod->lx_fread(buf, obj_pg_sta->o32_pagesize, 1, lx_exe_mod);

        // unpack page
        io_log("unpacking page to %x\n", tmp_vm_code);
        ret = unpack_page(tmp_vm_code, buf, obj_pg_sta);

        io_log("ret=%d\n", ret);

        if (! ret)
        {
            //ret = 1;
            io_log("page unpack error!\n");
            break;
        }

        code_mmap_pos += 0x1000;
    }


    return ret;
}


/* @brief Applies fixups to all objects except imported functions.
   Used for programs and dlls.
   ret_rc is an OS/2 error number. */

int do_fixup_code_data_lx(struct LX_module *lx_exe_mod, int *ret_rc)
{
    unsigned long int i;
    /* If there is a code object (with a main function) then do a fixup on it and
       it's data/stack object if it exists.*/
    for (i = 1; i <= get_obj_num(lx_exe_mod); i++)
    {
        struct o32_obj *obj = get_obj(lx_exe_mod, i);
        if (obj != 0)
        {
            if (! do_fixup_obj_lx(lx_exe_mod, obj, ret_rc))
            {
                return 0; /* Some error happened, return false and forward error in ret_rc. */
            }
        }
    }


    return 1;
}


/* Internal Fixup*/
void apply_internal_fixup(struct LX_module *lx_exe_mod, struct r32_rlc *min_rlc, unsigned long int vm_start_of_page)
{
    short srcoff_cnt1;
    int addit = 0;
    int additive_size = 0;
    int object1;
    //int trgoffs;
    struct o32_obj *target_object;
    unsigned long vm_start_target_obj;
    unsigned long vm_source;
    unsigned long vm_target;
    int trg_off_size = 0;
    int object_size = 0;
    int i, cnt;

    if (min_rlc->nr_flags & 0x04) // additive present
        additive_size = 2;

    if (min_rlc->nr_flags & 0x20) // 32-bit additive field
        additive_size = 4;

    if (additive_size)
        addit = get_additive_rlc(min_rlc);

    if (min_rlc->nr_flags & 0x10) // 32-bit target offset flag
        trg_off_size = 4;
    else
        trg_off_size = 2;

    if ( (min_rlc->nr_stype & NRSRCMASK) == 0x02) // 16-bit selector fixup
        trg_off_size = 0;

    if (min_rlc->nr_flags & 0x40) // 16-bit object number/module ordinal flag
        object_size = 2;
    else
        object_size = 1;

    if (min_rlc->nr_stype & NRCHAIN)
    {
        short *srcoff = (short *)
            ((char *)min_rlc + 3 * 1 + object_size + trg_off_size + additive_size);

        object1 = get_mod_ord1_rlc(min_rlc); /* On the same offset as Object1. */
        //trgoffs = get_trg_off_size(min_rlc);

        target_object = get_obj(lx_exe_mod, object1);
        ////vm_start_target_obj = target_object->o32_reserved;
        vm_start_target_obj = target_object->o32_base;

        /* Get address of target offset and put in source offset. */
        vm_target = vm_start_target_obj + addit;

        if (trg_off_size)
        {
            // 16-bit selector fixup has target offset missing
            vm_target += get_imp_ord1_rlc(min_rlc);
        }

        cnt = get_srcoff_cnt1_rlc(min_rlc);

        //io_log("!src=%02x, trg=%02x, cnt=%02x, obj#=%02x, trgoff=%04x, addit=%d\nsrcoffs = ",
        //        min_rlc->nr_stype, min_rlc->nr_flags, cnt,
        //        min_rlc->r32_objmod, get_imp_ord1_rlc(min_rlc), addit);

        for (i = 0; i < cnt; i++)
        {
            srcoff_cnt1 = srcoff[i];
            vm_source = vm_start_of_page + srcoff_cnt1;

            apply_fixup(min_rlc->nr_stype & 0xf, vm_source, vm_target);
        }

        //io_log("\n");
    }
    else
    {
        srcoff_cnt1 = get_srcoff_cnt1_rlc(min_rlc);
        object1 = get_mod_ord1_rlc(min_rlc); /* On the same offset as Object1. */
        //trgoffs = get_trg_off_size(min_rlc);

        target_object = get_obj(lx_exe_mod, object1);
        ////vm_start_target_obj = target_object->o32_reserved;
        vm_start_target_obj = target_object->o32_base;

        /* Get address of target offset and put in source offset. */
        vm_target = vm_start_target_obj + addit;

        if (trg_off_size)
        {
            // 16-bit selector fixup has target offset missing
            vm_target += get_imp_ord1_rlc(min_rlc);
        }

        vm_source = vm_start_of_page + srcoff_cnt1;

        //io_log("vm_target=%x, vm_start_target_obj=%x, get_imp_ord1_rlc(min_rlc)=%x, addit=%x\n", vm_target, vm_start_target_obj, get_imp_ord1_rlc(min_rlc), addit);
        //io_log("!src=%02x, trg=%02x, srcoff=%04x, obj#=%02x, trgoff=%04x, addit=%d\n",
        //        min_rlc->nr_stype, min_rlc->nr_flags, srcoff_cnt1,
        //        min_rlc->r32_objmod, get_imp_ord1_rlc(min_rlc), addit);

        apply_fixup(min_rlc->nr_stype & 0xf, vm_source, vm_target);
    }
}


/* Internal Entry Table Fixup*/
void apply_internal_entry_table_fixup(struct LX_module *lx_exe_mod, struct r32_rlc *min_rlc, unsigned long int vm_start_of_page)
{
    enum
    {
        UNUSED_ENTRY_SIZE = 2,
        ENTRY_HEADER_SIZE = 4, /* For all entries except UNUSED ENTRY.*/
        _16BIT_ENTRY_SIZE = 3,
        _286_CALL_GATE_ENTRY_SIZE = 5,
        _32BIT_ENTRY_SIZE         = 5,
        FORWARD_ENTRY_SIZE        = 7
    };

    short srcoff_cnt1;
    int cnt;
    //int addit = 0;
    int additive_size = 0;
    int object_size;
    int offs_to_entry_tbl;
    struct b32_bundle *entry_table_start;
    struct b32_bundle *entry_table;
    struct e32_entry  *entry = NULL;
    char              *cptr_ent_tbl;
    unsigned long     i, ordinal;
    unsigned long     cbEntries;
    unsigned long     offset = 0;
    unsigned short    target_object = 0;
    unsigned long     vm_source;
    unsigned long     vm_target;

    /* Offset to Entry Table inside the Loader Section. */
    offs_to_entry_tbl = lx_exe_mod->lx_head_e32_exe->e32_enttab - lx_exe_mod->lx_head_e32_exe->e32_objtab;
    entry_table_start = (struct b32_bundle *) &lx_exe_mod->loader_section[offs_to_entry_tbl];
    entry_table       = entry_table_start;
    cptr_ent_tbl      = &lx_exe_mod->loader_section[offs_to_entry_tbl];

    ordinal   = get_mod_ord1_rlc(min_rlc);
    cbEntries = 1;

    while (entry_table->b32_cnt)
    {
        switch (entry_table->b32_type)
        {
            case EMPTY:
                for (i = 0; i < entry_table->b32_cnt; i++, cbEntries++)
                {
                    if (ordinal == cbEntries)
                        break;

                    cptr_ent_tbl += UNUSED_ENTRY_SIZE;
                }
                entry_table  = (struct b32_bundle *)cptr_ent_tbl;
                break;

            case ENTRYFWD:
                cptr_ent_tbl   += ENTRY_HEADER_SIZE;

                for (i = 0; i < entry_table->b32_cnt; i++, cbEntries++)
                {
                    if (ordinal == cbEntries)
                        break;

                    cptr_ent_tbl   += FORWARD_ENTRY_SIZE;
                }

                entry_table  = (struct b32_bundle *)cptr_ent_tbl;
                break;

            case ENTRY32:
                cptr_ent_tbl   += ENTRY_HEADER_SIZE;

                for (i = 0; i < entry_table->b32_cnt; i++, cbEntries++)
                {
                    if (ordinal == cbEntries)
                        break;

                    cptr_ent_tbl   += _32BIT_ENTRY_SIZE;
                }

                if (i < entry_table->b32_cnt)
                {
                    entry = (struct e32_entry *)cptr_ent_tbl;
                    target_object = entry_table->b32_obj;
                    offset = entry->e32_variant.e32_offset.offset32;
                    break;
                }

                entry_table  = (struct b32_bundle *)cptr_ent_tbl;
                break;

            case GATE16:
                cptr_ent_tbl   += ENTRY_HEADER_SIZE;

                for (i = 0; i < entry_table->b32_cnt; i++, cbEntries++)
                {
                    if (ordinal == cbEntries)
                        break;

                    cptr_ent_tbl   += _286_CALL_GATE_ENTRY_SIZE;
                }

                entry_table  = (struct b32_bundle *)cptr_ent_tbl;
                break;

            case ENTRY16:
                cptr_ent_tbl   += ENTRY_HEADER_SIZE;

                for (i = 0; i < entry_table->b32_cnt; i++, cbEntries++)
                {
                    if (ordinal == cbEntries)
                        break;

                    cptr_ent_tbl   += _16BIT_ENTRY_SIZE;
                }

                if (i < entry_table->b32_cnt)
                {
                    entry = (struct e32_entry *)cptr_ent_tbl;
                    target_object = entry_table->b32_obj;
                    offset = entry->e32_variant.e32_offset.offset16;
                    break;
                }

                entry_table  = (struct b32_bundle *)cptr_ent_tbl;
                break;

            default:
                io_log("Invalid entry type! %d, entry table %p\n",
                       entry_table->b32_type, entry_table);
                return; /* Invalid entry */
        }

        if (ordinal == cbEntries)
            break;
    }

    if (entry_table->b32_cnt && i < entry_table->b32_cnt)
    {
        switch (entry_table->b32_type)
        {
            case ENTRY16:
            case ENTRY32:
                srcoff_cnt1 = get_srcoff_cnt1_rlc(min_rlc);

                if (min_rlc->nr_flags & 0x04) // additive present
                    additive_size = 2;

                if (min_rlc->nr_flags & 0x20) // 32-bit additive field
                    additive_size = 4;

                //if (additive_size)
                //    addit = get_additive_rlc(min_rlc);

                if (min_rlc->nr_stype & NRCHAIN)
                {
                    if (min_rlc->nr_flags & 0x40) // 16-bit object number/module ordinal flag
                        object_size = 2;
                    else
                        object_size = 1;

                    short *srcoff = (short *)
                        ((char *)min_rlc + 3 * 1 + object_size + additive_size);

                    vm_target = get_obj(lx_exe_mod, target_object)->o32_base + offset;
                    cnt = get_srcoff_cnt1_rlc(min_rlc);

                    for (i = 0; i < cnt; i++)
                    {
                        srcoff_cnt1 = srcoff[i];
                        vm_source = vm_start_of_page + srcoff_cnt1;

                        apply_fixup(min_rlc->nr_stype & 0xf, vm_source, vm_target);
                    }
                }
                else
                {
                    vm_source = vm_start_of_page + srcoff_cnt1;
                    vm_target = get_obj(lx_exe_mod, target_object)->o32_base + offset;

                    apply_fixup(min_rlc->nr_stype & 0xf, vm_source, vm_target);
                }
                break;

            default:
                io_log("Invalid entry type: %u\n", entry_table->b32_type);
                return;
        }
    }
}


void apply_fixup(int type, unsigned long vm_source, unsigned long vm_target)
{
    unsigned short flat_cs = 0;

    asm volatile("movw %%cs, %%ax\n\t"
                 "movw %%ax, %[flat_cs]\n\t"::[flat_cs] "m" (flat_cs));

    switch (type)
    {
        case 0x00: // 8-bit fixup
            {
                unsigned char *ptr_source;

                ptr_source = (unsigned char *)vm_source;
                *ptr_source = (unsigned char)vm_target;
            }
            break;

        case 0x02: // 16-bit selector fixup
            {
                unsigned short *ptr_source;
                unsigned long ptr = flat2sel(vm_target);

                //io_log("^^^ vm_target=%x\n", vm_target);

                ptr_source = (unsigned short *)vm_source;
                *ptr_source = (unsigned short)(ptr >> 16);
            }
            break;

        case 0x03: // 16:16 pointer fixup
            {
                unsigned long *ptr_source;
                unsigned long ptr = flat2sel(vm_target);

                ptr_source = (unsigned long *)vm_source;
                *ptr_source = (unsigned long)ptr;
            }
            break;

        case 0x05: // 16-bit offset fixup
            {
                unsigned short *ptr_source;
                unsigned long ptr = flat2sel(vm_target);

                ptr_source = (unsigned short *)vm_source;
                *ptr_source = (unsigned short)(ptr & 0xffff);
            }
            break;

        case 0x06: // 16:32 pointer fixup
            {
                unsigned short *ptr_source;

                ptr_source = (unsigned short *)vm_source;
                *(unsigned long *)ptr_source = vm_target;
                ptr_source += 2;
                *ptr_source = flat_cs;
            }
            break;

        case 0x07: // 32-bit fixup
            {
                unsigned long *ptr_source; // Address to fixup

                ptr_source = (unsigned long *)vm_source;
                *ptr_source = vm_target;
            }
            break;

        case 0x08: // 32-bit self-relative fixup
            {
                unsigned long *ptr_source; // Address to fixup

                ptr_source = (unsigned long *)vm_source;
                *ptr_source += vm_target - 4;
            }
            break;

        default:
            io_log("Unsupported fixup src: %u\n", type);
    }
}


/* Applies fixups for an object. Returns true(1) or false(0) to show status.*/
int do_fixup_obj_lx(struct LX_module *lx_exe_mod,
                    struct o32_obj *lx_obj, int *ret_rc)
{
    //int ord_found;
    char *pas_imp_proc_name;
    int import_name_offs;
    char buf_import_name[260];
    //char cont_buf_mod_name[255];
    char *mod_name;
    //char *cont_mod_name;
    ////int import_ord;
    int mod_nr;
    //unsigned long rc;
    //unsigned long int *ptr_source;
    //unsigned long int vm_source;
    //unsigned long int vm_target;
    //unsigned long int vm_start_target_obj;
    //struct o32_obj *target_object;
    char buf_mod_name[255];
    char *org_mod_name;
    //struct LX_module *found_module;
    //int trgoffs;
    //int object1;
    ////int addit = 0;
    ////int additive_size = 0;
    ////int srcoff_cnt1;
    ////int fixup_source_flag;
    int fixup_source;
    int fixup_offset;
    ////char *import_name;
    unsigned long int vm_start_of_page;
    struct r32_rlc *min_rlc;
    int pg_offs_fix;
    int pg_end_offs_fix;
    int page_nr = 0;
    int startpage = lx_obj->o32_pagemap;
    int lastpage  = lx_obj->o32_pagemap + lx_obj->o32_mapsize;
    //UCHAR uchLoadError[CCHMAXPATH] = {0}; /* Error info from DosExecPgm */

    //io_log("--------------------Listing fixup data ------------------------- %p\n", lx_obj);

    /* Goes through every page of the object.
       The fixups are variable size and a bit messy to traverse. */

    for(page_nr=startpage; page_nr < lastpage; page_nr++)
    {
        if (options.debugixfmgr)
            io_log("-----  Object %d of %d\n",startpage, lastpage);

        /* Go and get byte position for fixup from the page logisk_sida.
           Start offset for fixup in the page */
        pg_offs_fix = get_fixup_pg_tbl_offs(lx_exe_mod, page_nr);

        /* Offset for next page.*/
        pg_end_offs_fix = get_fixup_pg_tbl_offs(lx_exe_mod, page_nr+1);

        /* Fetches a relocations structure from offset pg_offs_fix. */
        min_rlc = get_fixup_rec_tbl_obj(lx_exe_mod, pg_offs_fix);

        fixup_offset = pg_offs_fix;

        /* Get the memory address for the page. The page number is
           from the beginning of all pages a need to be adapted to the beginning of this
           object. */
        //vm_start_of_page = lx_obj->o32_base +
        vm_start_of_page = lx_obj->o32_reserved +
            get_e32_pagesize(lx_exe_mod) * (page_nr-lx_obj->o32_pagemap);

        /*
        This loop traverses the fixups and increases
        the pointer min_rlc with the size of previous fixup.
        while(min_rlc is within the offset of current page) {
        */
        while(fixup_offset < pg_end_offs_fix)
        {
            int fixup_size;

            min_rlc = get_fixup_rec_tbl_obj(lx_exe_mod, fixup_offset);
            print_struct_r32_rlc_info(min_rlc);
            fixup_size = get_reloc_size_rlc(min_rlc);

            fixup_source = min_rlc->nr_stype & 0xf;
            ////fixup_source_flag = min_rlc->nr_stype & 0xf0;

            switch (min_rlc->nr_flags & NRRTYP)
            {
                case NRRINT:
                    apply_internal_fixup(lx_exe_mod, min_rlc, vm_start_of_page);
                    break;

                case NRRENT:
                    apply_internal_entry_table_fixup(lx_exe_mod, min_rlc, vm_start_of_page);
                    break;

                case NRRORD:
                {   /* Import by ordinal */
                    /* Indata: lx_exe_mod, min_rlc */
                    mod_nr = get_mod_ord1_rlc(min_rlc); // Request module number
                    ////import_ord = get_imp_ord1_rlc(min_rlc); // Request ordinal number
                    ////srcoff_cnt1 = get_srcoff_cnt1_rlc(min_rlc);
                    ////addit = get_additive_rlc(min_rlc);

                    mod_name = (char*)&buf_mod_name;

                    /* Get name of imported module. */
                    org_mod_name = get_imp_mod_name(lx_exe_mod,mod_nr);
                    copy_pas_str(mod_name, org_mod_name);

#if 0
                    /* Look for module if it's already loaded, if it's not try to load it.*/
                    rc = ModLoadModule(uchLoadError, sizeof(uchLoadError),
                            mod_name, (unsigned long *)&found_module);

                    if (found_module)
                        found_module=(struct LX_module *)(((IXFModule *)found_module)->FormatStruct);

                    if (! found_module)
                    {   /* Unable to find and load module. */
                        io_log("Can't find module: '%s' \n", mod_name);
                        *ret_rc = rc;
                        return 0;
                    }

                    cont_mod_name = (char*)&cont_buf_mod_name;
                    copy_pas_str(cont_mod_name, get_module_name_res_name_tbl_entry(lx_exe_mod));

                    if (! apply_import_fixup(lx_exe_mod, found_module, lx_obj, mod_nr, import_ord,
                                addit, srcoff_cnt1, min_rlc, ret_rc))
                    {
                        char tmp_buf[255];
                        char *s_buf = (char *)&tmp_buf[0];

                        copy_pas_str(s_buf, get_imp_proc_name(found_module,import_ord));
                        io_log("Import error in '%s', can't find '%s'(%d)\n",
                            mod_name, s_buf, import_ord);
                        *ret_rc = 182; /* ERROR_ORDINAL_NOT_FOUND 182, ERROR_FILE_NOT_FOUND 2*/
                        return 0;
                    }
#endif

                }
                break;

                case NRRNAM:
                {   /* Import by name */
                    //io_log("Import by name \n");
                    mod_nr = get_mod_ord1_rlc(min_rlc);
                    import_name_offs = get_imp_ord1_rlc(min_rlc);
                    ////srcoff_cnt1 = get_srcoff_cnt1_rlc(min_rlc);
                    ////addit = get_additive_rlc(min_rlc);

                    pas_imp_proc_name = get_imp_proc_name(lx_exe_mod, import_name_offs);
                    copy_pas_str(buf_import_name, pas_imp_proc_name);
                    ////import_name = (char*)&buf_import_name;
                    mod_name = (char*)&buf_mod_name;
                    /* Get name of imported module. */
                    org_mod_name = get_imp_mod_name(lx_exe_mod,mod_nr);
                    copy_pas_str(mod_name, org_mod_name);
#if 0
                    /* Look for module if it's already loaded, if it's not try to load it.*/
                    rc = ModLoadModule(uchLoadError, sizeof(uchLoadError),
                             mod_name, (unsigned long *)&found_module);

                    if (found_module)
                        found_module = (struct LX_module *)(((IXFModule *)found_module)->FormatStruct);

                    if (! found_module)
                    {   /* Unable to find and load module. */
                        //io_log("Can't find module: '%s' \n", mod_name);
                        *ret_rc = rc;
                        return 0;
                    }

                    ord_found = get_res_name_tbl_entry(found_module, buf_import_name);

                    if (ord_found == 0)
                        ord_found = get_non_res_name_tbl_entry(found_module, buf_import_name);


                    if (! apply_import_fixup(lx_exe_mod, found_module, lx_obj, mod_nr,
                        ord_found, addit, srcoff_cnt1, min_rlc, ret_rc))
                    {
                        char tmp_buf[255];
                        char *s_buf = (char *)&tmp_buf[0];

                        copy_pas_str(s_buf, get_imp_proc_name(found_module,import_ord));
                        io_log("Import error in '%s', can't find '%s'\n", mod_name, s_buf);
                        *ret_rc = 182; /* ERROR_ORDINAL_NOT_FOUND 182, ERROR_FILE_NOT_FOUND 2*/
                        return 0;
                    }


                    /* TODO Translate to English!
                       Leta efter funktionen  buf_import_name i mod_name. Den är en sträng
                       så skapa funktioner att leta i Entry Table i mod_name.
                       Nåt med en pascalsträng och en ordinal, använd ordinalen som
                       vanligt och leta upp funktionen med den.
                     */
#endif

                }
                break;

                default:
                    io_log("Unsupported Fixup! SRC: 0x%x \n", fixup_source);
                    return 0; /* Is there any OS/2 error number for this? */
            } /* switch(fixup_source) */

            fixup_offset += fixup_size;

        } /* while(fixup_offset < pg_end_offs_fix) { */
    }


    return 1;
}

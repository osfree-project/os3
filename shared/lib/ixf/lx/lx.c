/* OS/2 API defs */
#define INCL_DOS
#define INCL_BSEDOS
#define INCL_DOSEXCEPTIONS
#define INCL_DOSPROCESS
#define INCL_ERRORS
#include <os2.h>

#ifndef ENEWHDR
  #define ENEWHDR 0x3c
#endif

/* osFree OS/2 personality internal */
#include <os3/lx.h>
#include <os3/io.h>
#include <os3/modlx.h>
#include <os3/loadobjlx.h>
#include <os3/fixuplx.h>
#include <os3/cfgparser.h>

/* libc includes */
#include <stdlib.h>
#include <string.h>

#include <exe.h>

#define ISDLL(ixf) (E32_MFLAGS(*((struct LX_module *)(ixf->FormatStruct))->lx_head_e32_exe) & E32MODMASK) == E32MODDLL
//#define ISPIC(ixf) (E32_MFLAGS(*((struct LX_module *)(ixf->FormatStruct))->lx_head_e32_exe) & E32NOINTFIX)
#define ISPIC(ixf) ISDLL(ixf)

// Prototypes
unsigned long convert_entry_table_to_BFF(IXFModule *ixfModule);
unsigned long convert_fixup_table_to_BFF(IXFModule *ixfModule);
unsigned long calc_imp_fixup_obj_lx(struct LX_module *lx_exe_mod,
                                struct o32_obj *lx_obj, int *ret_rc);
int convert_imp_fixup_obj_lx(IXFModule *ixfModule,
                                struct o32_obj *lx_obj, int *ret_rc, unsigned long *fixup_counter);
void dump_header_mz(struct exe hdr);
void dump_header_lx(struct e32_exe hdr);

int LXLoadObjects(IXFModule *ixf);

/* Read in the header for the file from a memory buffer.
   ALso an constructor for struct LX_module. */
struct LX_module *LXLoadStream(char *stream_fh, int str_size, struct LX_module *lx_mod);


unsigned long LXIdentify(void *addr, unsigned long size)
{
    unsigned long lx_module_header_offset = 0;

    if (((*(char *)addr == 'M') && (*((char *)addr + 1) == 'Z')) ||
        ((*(char *)addr == 'Z') && (*((char *)addr + 1) == 'M')))
    {
        /* Found DOS stub. Get offset of LX module. */
        lx_module_header_offset = *(unsigned int *)((unsigned long)addr + ENEWHDR);
    }

    if ((*((char *)addr + lx_module_header_offset) == 'L') &&
        (*((char *)addr + lx_module_header_offset+1) == 'X'))
    {
        #ifdef __OS2__
        // This is check for internal relocations support. Specific for OS/2 host because
        // it is hard to manage virtual memory and processes without kernel driver support.
        // We don't want to loss forces for such driver so debug only code with internal relocations
        // support
        #endif
        return NO_ERROR;
    };


    return ERROR_BAD_FORMAT;
}

unsigned long LXLoad(void *addr, unsigned long size, void *ixfModule)
{
    unsigned long rc;
    unsigned long module_counter;
    char buf[256];
    IXFModule *ixf;
    struct o32_obj *code;
    struct o32_obj *stack;

    ixf = (IXFModule*)ixfModule;

    ixf->FormatStruct = malloc(sizeof(struct LX_module));

    /* A file from a buffer.*/
    if ( LXLoadStream((char *)addr, size, (struct LX_module *)(ixf->FormatStruct)) )
    {
        /* Convert imported module table to BFF format */
        ixf->cbModules = ((struct LX_module*)(ixf->FormatStruct))->lx_head_e32_exe->e32_impmodcnt;
        ixf->Modules = (char **)malloc(sizeof(char *) * ixf->cbModules);

        for (module_counter = 1;
             module_counter < ixf->cbModules + 1;
             module_counter++)
        {
            get_imp_mod_name_cstr((struct LX_module *)(ixf->FormatStruct), module_counter, (char *)&buf, sizeof(buf));
            ixf->Modules[module_counter-1] = (char *)malloc(strlen(buf) + 1);
            strcpy(ixf->Modules[module_counter - 1], buf);
        }

        if (ISPIC(ixf))
        {
            ixf->PIC = 1;
        }
        else
        {
            ixf->PIC = 0;
        }

        /* Load all objects in dll.*/
        LXLoadObjects(ixf);

        /* Convert entry table to BFF format for DLL */
        if (ISDLL(ixf))
        {
            rc = convert_entry_table_to_BFF(ixf);

            if (rc)
            {
                return rc;
            }
        }

        /* Convert fixup table to BFF format */
        convert_fixup_table_to_BFF(ixf);

        /* Set entry point */
        code = (struct o32_obj *)get_code((struct LX_module *)(ixf->FormatStruct));

        if (code == NULL)
        //{
            ixf->EntryPoint = NULL;
        //} else {
        //  ixf->EntryPoint = get_eip((struct LX_module *)(ixf->FormatStruct)) + code->o32_base;
        //}

        stack = (struct o32_obj *)get_data_stack((struct LX_module *)(ixf->FormatStruct));

        if (stack == NULL)
        //{
            ixf->Stack = NULL;
        //} else {
        //  ixf->Stack = get_esp((struct LX_module *)(ixf->FormatStruct)) + stack->o32_base;
        //}

    }


    return NO_ERROR;
}

unsigned long LXFixup(void * lx_exe_mod)
{
    int rc = NO_ERROR;

    /* Apply internal fixups. */
    do_fixup_code_data_lx((struct LX_module *)lx_exe_mod, &rc);

    return rc;
}

/* converts FLAT address to 16:16 address */
unsigned long flat2sel(unsigned long addr)
{
  unsigned short sel;
  unsigned short offs;

  if (! addr)
  {
    return addr;
  }

  sel = addr >> 16;
  sel = (sel << 3) | 7;
  offs = addr & 0xffff;

  addr = (sel << 16) | offs;

  return addr;
}


/* struct e32_exe {
   unsigned long       e32_objtab;     // Object table offset
   unsigned long       e32_enttab;     // Offset of Entry Table

   struct b32_bundle
{
    unsigned char       b32_cnt;        // Number of entries in this bundle
    unsigned char       b32_type;       // Bundle type
    unsigned short      b32_obj;        // Object number


 An entry is a description of an exported function with info about where it is inside
 an object and it's offset inside that object. This info is used to apply fixups.

 Type of Entries:
        Unused Entry (2 bytes in size): (Numbers to skip over)
                CNT  TYPE

        16-bit Entry (7 bytes in size):
                CNT  TYPE  OBJECT  FLAGS  OFFSET16

        286 Call Gate Entry (9 bytes in size):
                CNT  TYPE  OBJECT  FLAGS  OFFSET16  CALLGATE

        32-bit Entry (9 bytes in size):
                CNT  TYPE  OBJECT  FLAGS  OFFSET

        Forwarder Entry (9 bytes in size):
                CNT  TYPE  RESERVED  FLAGS  MOD_ORD#  OFFSET/ORDNUM

        Field sizes:
                CNT, TYPE, FLAGS, = DB
                OBJECT, OFFSET16, CALLGATE, RESERVED, MOD_ORD# = DW
                OFFSET, ORDNUM = DD
 ----------------------------------------------------------------
  Get's the function number in entry_ord_to_search from the module lx_mod.
  Returns data in the pointers beginning with "ret_*"
   */


unsigned long convert_entry_table_to_BFF(IXFModule *ixfModule)
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

    char buf[256];
    //unsigned long cbEntries;   /* Number of items in entry table */
    struct LX_module *lx_mod; /* LX_format structure */

    int offs_to_entry_tbl;
    struct b32_bundle *entry_table_start;
    struct b32_bundle *entry_table; //,
    //                  *prev_entry_table;
    char *cptr_ent_tbl;
    //struct e32_entry *entry_post;
    //char bbuf[3];
    //int entry_ord_index;
    //int prev_ord_index;
    //int unused_entry;
    unsigned long int i_cptr_ent_tbl;
    //int elements_in_bundle;
    unsigned long i;


    lx_mod = (struct LX_module *)(ixfModule->FormatStruct);
    /* Offset to Entry Table inside the Loader Section. */
    offs_to_entry_tbl = lx_mod->lx_head_e32_exe->e32_enttab - lx_mod->lx_head_e32_exe->e32_objtab;

    entry_table_start = (struct b32_bundle *)&lx_mod->loader_section[offs_to_entry_tbl];

    entry_table = entry_table_start;
    cptr_ent_tbl = &lx_mod->loader_section[offs_to_entry_tbl];

    ixfModule->cbEntries = 0;

    /* Count number of entries */
    while (1)
    {
        if (entry_table->b32_cnt == 0)
            /* end of table*/
            break;

        switch (entry_table->b32_type)
        {
            case EMPTY: /* Unused Entry, just skip over them.*/
            {
                ixfModule->cbEntries += entry_table->b32_cnt;
                cptr_ent_tbl += UNUSED_ENTRY_SIZE;
                entry_table = (struct b32_bundle *)cptr_ent_tbl;
            }
            break;

            case ENTRY32:
            {   /* Jump over that bundle. */
                cptr_ent_tbl = (char *)entry_table;
                cptr_ent_tbl += ENTRY_HEADER_SIZE;
                i_cptr_ent_tbl = (unsigned long)cptr_ent_tbl;
                ixfModule->cbEntries += entry_table->b32_cnt;
                i_cptr_ent_tbl += _32BIT_ENTRY_SIZE * entry_table->b32_cnt;

                cptr_ent_tbl = (char *)i_cptr_ent_tbl;
                entry_table = (struct b32_bundle *)cptr_ent_tbl;
            }
            break;

            case ENTRY16:
            {   /* Jump over that bundle. */
                cptr_ent_tbl = (char *)entry_table;
                cptr_ent_tbl += ENTRY_HEADER_SIZE;
                i_cptr_ent_tbl = (unsigned long)cptr_ent_tbl;
                ixfModule->cbEntries += entry_table->b32_cnt;
                i_cptr_ent_tbl += _16BIT_ENTRY_SIZE * entry_table->b32_cnt;

                cptr_ent_tbl = (char *)i_cptr_ent_tbl;
                entry_table = (struct b32_bundle *)cptr_ent_tbl;
            }
            break;

            case ENTRYFWD:
            {   /* Jump over that bundle. */
                cptr_ent_tbl = (char *)entry_table;
                cptr_ent_tbl = &cptr_ent_tbl[ENTRY_HEADER_SIZE];
                i_cptr_ent_tbl = (unsigned long int)cptr_ent_tbl;
                ixfModule->cbEntries += entry_table->b32_cnt;
                i_cptr_ent_tbl += FORWARD_ENTRY_SIZE * entry_table->b32_cnt;

                cptr_ent_tbl = (char *)i_cptr_ent_tbl;
                entry_table = (struct b32_bundle *)cptr_ent_tbl;
            }
            break;

            default:
                io_log("Unsupported entry type! %d, entry_table: %p\n",
                       entry_table->b32_type, entry_table);
                return 1; /* Invalid entry. Return ERROR_INVALID_FUNCTION */
        }
    }

    /* Allocate memory for entries table */
    ixfModule->Entries = (IXFMODULEENTRY *)malloc(ixfModule->cbEntries * sizeof(IXFMODULEENTRY));

    if (! ixfModule->Entries)
    {
        io_log("Insufficient memory for entry table!\n");
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    /* Fill entries table in BFF format */
    entry_table = entry_table_start;
    cptr_ent_tbl = &lx_mod->loader_section[offs_to_entry_tbl];

    ixfModule->cbEntries = 1;

    while (1)
    {
        if (entry_table->b32_cnt == 0)
        {
            ixfModule->cbEntries--;
            /* end of table */
            break;
        }

        if (options.debugixfmgr)
        {
            io_log("number of entries in bundle %d\n",entry_table->b32_cnt);
            io_log("type=%d\n", entry_table->b32_type);
        }

        switch (entry_table->b32_type)
        {
            case EMPTY: /* Unused Entry, just skip over them.*/
            {
                for (i = ixfModule->cbEntries; i < ixfModule->cbEntries + entry_table->b32_cnt; i++)
                {
                    ixfModule->Entries[i - 1].FunctionName = NULL;
                    ixfModule->Entries[i - 1].Address = NULL;
                    ixfModule->Entries[i - 1].Ordinal = 0;
                    ixfModule->Entries[i - 1].ModuleName = NULL;
                }

                ixfModule->cbEntries += entry_table->b32_cnt;

                cptr_ent_tbl += UNUSED_ENTRY_SIZE;
                entry_table = (struct b32_bundle *)cptr_ent_tbl;
            }
            break;

            case ENTRY32:
            {
                cptr_ent_tbl = (char *)entry_table;
                cptr_ent_tbl += ENTRY_HEADER_SIZE;
                i_cptr_ent_tbl = (unsigned long)cptr_ent_tbl;

                for (i = ixfModule->cbEntries; i < ixfModule->cbEntries+entry_table->b32_cnt; i++)
                {
                    ixfModule->Entries[i - 1].FunctionName = NULL;
                    ixfModule->Entries[i - 1].Address = (void *)((unsigned long)((struct e32_entry *)(i_cptr_ent_tbl))->e32_variant.e32_offset.offset32 + get_obj(lx_mod, entry_table->b32_obj)->o32_base);
                    ixfModule->Entries[i - 1].Ordinal = 0;
                    ixfModule->Entries[i - 1].ModuleName = NULL;
                    i_cptr_ent_tbl += _32BIT_ENTRY_SIZE;
                }

                ixfModule->cbEntries += entry_table->b32_cnt;

                cptr_ent_tbl = (char *)i_cptr_ent_tbl;
                entry_table = (struct b32_bundle *)cptr_ent_tbl;
            }
            break;

            case ENTRY16:
            {
                cptr_ent_tbl = (char *)entry_table;
                cptr_ent_tbl += ENTRY_HEADER_SIZE;
                i_cptr_ent_tbl = (unsigned long)cptr_ent_tbl;

                for (i = ixfModule->cbEntries; i < ixfModule->cbEntries+entry_table->b32_cnt; i++)
                {
                    ixfModule->Entries[i - 1].FunctionName = NULL;
                    ixfModule->Entries[i - 1].Address = (void *)((unsigned long)((struct e32_entry *)(i_cptr_ent_tbl))->e32_variant.e32_offset.offset16 + flat2sel(get_obj(lx_mod, entry_table->b32_obj)->o32_base));
                    ixfModule->Entries[i - 1].Ordinal = 0;
                    ixfModule->Entries[i - 1].ModuleName = NULL;
                    i_cptr_ent_tbl += _16BIT_ENTRY_SIZE;
                }

                ixfModule->cbEntries += entry_table->b32_cnt;

                cptr_ent_tbl = (char *)i_cptr_ent_tbl;
                entry_table = (struct b32_bundle *)cptr_ent_tbl;
            }
            break;

            case ENTRYFWD:
            {
                cptr_ent_tbl = (char *)entry_table;
                cptr_ent_tbl = &cptr_ent_tbl[ENTRY_HEADER_SIZE];
                i_cptr_ent_tbl = (unsigned long)cptr_ent_tbl;

                for (i = ixfModule->cbEntries; i < ixfModule->cbEntries + entry_table->b32_cnt; i++)
                {
                    ixfModule->Entries[i - 1].FunctionName = NULL;
                    ixfModule->Entries[i - 1].Address = NULL;
                    ixfModule->Entries[i - 1].Ordinal = (int)((struct e32_entry *)(i_cptr_ent_tbl))->e32_variant.e32_fwd.value;
                    copy_pas_str((char *)&buf, get_imp_mod_name((struct LX_module *)(ixfModule->FormatStruct),
                         ((struct e32_entry *)(i_cptr_ent_tbl))->e32_variant.e32_fwd.modord));
                    ixfModule->Entries[i - 1].ModuleName = (char *)malloc(strlen(buf) + 1);
                    strcpy(ixfModule->Entries[i - 1].ModuleName, buf);
                    i_cptr_ent_tbl += FORWARD_ENTRY_SIZE;
                }

                ixfModule->cbEntries += entry_table->b32_cnt;

                cptr_ent_tbl = (char*)i_cptr_ent_tbl;
                entry_table = (struct b32_bundle *)cptr_ent_tbl;
            }
            break;

            default:
                io_log("Invalid entry type! %d, entry_table: %p\n",
                       entry_table->b32_type, entry_table);
                return 0; /* Invalid entry.*/
        }
    }


    return 0;
}


unsigned long calc_imp_fixup_obj_lx(struct LX_module *lx_exe_mod,
                                struct o32_obj *lx_obj, int *ret_rc)
{
    unsigned long fixups;

    //int ord_found;
    //char *pas_imp_proc_name;
    //int import_name_offs;
    //char buf_import_name[260];
    //char cont_buf_mod_name[255];
    //char * mod_name;
    //char * cont_mod_name;
    //int import_ord;
    //int mod_nr;
    //unsigned long rc;
    //unsigned long int * ptr_source;
    //unsigned long int vm_source;
    //unsigned long int vm_target;
    //unsigned long int vm_start_target_obj;
    //struct o32_obj * target_object;
    //char buf_mod_name[255];
    //char * org_mod_name;
    //struct LX_module *found_module;
    //int trgoffs;
    //int object1;
    //int addit;
    //int srcoff_cnt1;
    //int fixup_source_flag;
    int fixup_source;
    int fixup_offset;
    //char *import_name;
    //unsigned long vm_start_of_page;
    struct r32_rlc *min_rlc;
    int pg_offs_fix;
    int pg_end_offs_fix;
    int page_nr = 0;
    int startpage = lx_obj->o32_pagemap;
    int lastpage  = lx_obj->o32_pagemap + lx_obj->o32_mapsize;

    fixups = 0;

    /* Goes through every page of the object.
       The fixups are variable size and a bit messy to traverse.*/

    for(page_nr = startpage; page_nr < lastpage; page_nr++)
    {
        /* Go and get byte position for fixup from the page logisk_sida.
           Start offset for fixup in the page*/
        pg_offs_fix = get_fixup_pg_tbl_offs(lx_exe_mod, page_nr);

        /* Offset for next page.*/
        pg_end_offs_fix = get_fixup_pg_tbl_offs(lx_exe_mod, page_nr + 1);

        /* Fetches a relocations structure from offset pg_offs_fix.*/
        min_rlc = get_fixup_rec_tbl_obj(lx_exe_mod, pg_offs_fix);

        fixup_offset = pg_offs_fix;

        /* Get the memory address for the page. The page number is
           from the beginning of all pages a need to be adapted to the beginning of this
           object. */
        //vm_start_of_page = lx_obj->o32_reserved +
        //                                get_e32_pagesize(lx_exe_mod) * (page_nr-lx_obj->o32_pagemap);

        /*
        This loop traverses the fixups and increases
        the pointer min_rlc with the size of previoues fixup.
        while(min_rlc is within the offset of current page) {
        */
        while(fixup_offset < pg_end_offs_fix)
        {
            min_rlc = get_fixup_rec_tbl_obj(lx_exe_mod, fixup_offset);
            print_struct_r32_rlc_info(min_rlc);

            fixup_source = min_rlc->nr_stype & 0xf;
            //fixup_source_flag = min_rlc->nr_stype & 0xf0;

            switch(min_rlc->nr_flags & NRRTYP)
            {
                case NRRINT:
                case NRRENT:
                    fixup_offset += get_reloc_size_rlc(min_rlc);
                    break;

                case NRRORD:
                {   /* Import by ordinal */
                    if (min_rlc->nr_stype & NRCHAIN)
                        fixups += min_rlc->r32_soff & 0xff;
                    else
                        fixups++;

                    fixup_offset += get_reloc_size_rlc(min_rlc);
                }
                break;

                case NRRNAM:
                {   /* Import by name */
                    if (min_rlc->nr_stype & NRCHAIN)
                        fixups += min_rlc->r32_soff & 0xff;
                    else
                        fixups++;

                    fixup_offset += get_reloc_size_rlc(min_rlc);
                }
                break;

                default:
                    io_log("Unsupported Fixup! SRC: 0x%x \n", fixup_source);
                    return 0; /* Is there any OS/2 error number for this? */
            } /* switch(fixup_source) */
        }     /* while(fixup_offset < pg_end_offs_fix) { */
    }


    return fixups;
}


unsigned long convert_fixup_table_to_BFF(IXFModule *ixfModule)
{
    unsigned long int i;
    unsigned long ret_rc, fixup_counter;

    ixfModule->cbFixups = 0;
    io_log("ixfModule->name=%s\n", ixfModule->name);

    /* If there is a code object (with a main function) then do a fixup on it and
       it's data/stack object if it exists.*/
    for(i = 1; i <= get_obj_num((struct LX_module *)(ixfModule->FormatStruct)); i++)
    {
        struct o32_obj *obj = get_obj((struct LX_module *)(ixfModule->FormatStruct), i);

        if (obj != 0)
            ixfModule->cbFixups += calc_imp_fixup_obj_lx((struct LX_module *)(ixfModule->FormatStruct), obj, (int *)&ret_rc);
    }

    if (ixfModule->cbFixups == 0)
    {
        ixfModule->Fixups = NULL;
        return 0;
    }

    ixfModule->Fixups = (IXFFIXUPENTRY *)malloc(ixfModule->cbFixups * sizeof(IXFFIXUPENTRY));

    /* Fill table... */
    fixup_counter = 0;
    for(i = 1; i <= get_obj_num((struct LX_module *)(ixfModule->FormatStruct)); i++)
    {
        struct o32_obj *obj = get_obj((struct LX_module *)(ixfModule->FormatStruct), i);

        if (obj != 0)
            convert_imp_fixup_obj_lx(ixfModule, obj, (int *)&ret_rc, &fixup_counter);
    }


    return 0; /* NO_ERROR */
}

/* Applies fixups for an object. Returns true(1) or false(0) to show status.*/
int convert_imp_fixup_obj_lx(IXFModule *ixfModule,
                                struct o32_obj *lx_obj, int *ret_rc,
                                unsigned long *fixup_counter)
{
    struct LX_module *lx_exe_mod;

    //int ord_found;
    char *pas_imp_proc_name;
    int import_name_offs;
    char buf_import_name[260];
    //char cont_buf_mod_name[255];
    char *mod_name;
    //char *cont_mod_name;
    int import_ord;
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
    int addit = 0;
    int additive_size = 0;
    short srcoff_cnt1;
    //int fixup_source_flag;
    int fixup_source;
    int fixup_offset;
    char *import_name;
    unsigned long int vm_start_of_page;
    unsigned long int start_of_page;
    struct r32_rlc *min_rlc;
    int pg_offs_fix;
    int pg_end_offs_fix;
    int page_nr = 0;
    int startpage = lx_obj->o32_pagemap;
    int lastpage  = lx_obj->o32_pagemap + lx_obj->o32_mapsize;
    //UCHAR uchLoadError[CCHMAXPATH] = {0}; /* Error info from DosExecPgm */
    //unsigned long fixup_counter;
    int i;

    //fixup_counter = 0;
    lx_exe_mod = (struct LX_module *)(ixfModule->FormatStruct);

    //io_log("--------------------Listing fixup data ------------------------- %p\n", lx_obj);

    /* Goes through every page of the object.
       The fixups are variable size and a bit messy to traverse.*/

    for (page_nr = startpage; page_nr < lastpage; page_nr++)
    {
        if (options.debugixfmgr)
            io_log("-----  Object %d of %d\n",startpage, lastpage);

        /* Go and get byte position for fixup from the page logisk_sida.
           Start offset for fixup in the page*/
        pg_offs_fix = get_fixup_pg_tbl_offs(lx_exe_mod, page_nr);

        /* Offset for next page.*/
        pg_end_offs_fix = get_fixup_pg_tbl_offs(lx_exe_mod, page_nr + 1);

        /* Fetches a relocations structure from offset pg_offs_fix.*/
        min_rlc = get_fixup_rec_tbl_obj(lx_exe_mod, pg_offs_fix);

        fixup_offset = pg_offs_fix;

        /* Get the memory address for the page. The page number is
           from the beginning of all pages a need to be adapted to the beginning of this
           object. */
        //vm_start_of_page = lx_obj->o32_base +
        vm_start_of_page = lx_obj->o32_reserved +
            get_e32_pagesize(lx_exe_mod) * (page_nr - lx_obj->o32_pagemap);
        start_of_page = lx_obj->o32_base +
            get_e32_pagesize(lx_exe_mod) * (page_nr - lx_obj->o32_pagemap);
        /*
        This loop traverses the fixups and increases
        the pointer min_rlc with the size of previoues fixup.
        while(min_rlc is within the offset of current page) {
        */
        while (fixup_offset < pg_end_offs_fix)
        {
            addit = 0;
            additive_size = 0;

            //io_log("fixup_offset=%lx\n", fixup_offset);
            min_rlc = get_fixup_rec_tbl_obj(lx_exe_mod, fixup_offset);
            //io_log("min_rlc=%lx\n", min_rlc);
            print_struct_r32_rlc_info(min_rlc);

            fixup_source = min_rlc->nr_stype & 0xf;
            //fixup_source_flag = min_rlc->nr_stype & 0xf0;

            switch (min_rlc->nr_flags & NRRTYP)
            {
                case NRRINT:
                case NRRENT:
                    /* Skip internal */
                    fixup_offset += get_reloc_size_rlc(min_rlc);
                    break;

                case NRRORD:
                {   /* Import by ordinal */
                    int import_ord_size = 0;
                    int mod_ord_size = 0;

                    /* Indata: lx_exe_mod, min_rlc */
                    mod_nr = get_mod_ord1_rlc(min_rlc); // Request module number
                    import_ord = get_imp_ord1_rlc(min_rlc); // Request ordinal number

                    if (min_rlc->nr_flags & 0x04) // additive present
                        additive_size = 2;

                    if (min_rlc->nr_flags & 0x20) // 32-bit additive field
                        additive_size = 4;

                    if (additive_size)
                        addit = get_additive_rlc(min_rlc);

                    if (min_rlc->nr_flags & 0x80) // 8-bit ordinal flag
                        import_ord_size = 1;
                    else if (min_rlc->nr_flags & 0x10) // 32-bit target offset flag
                        import_ord_size = 4;
                    else
                        import_ord_size = 2;

                    if (min_rlc->nr_flags & 0x40) // 16-bit object number/module ordinal flag
                        mod_ord_size = 2;
                    else
                        mod_ord_size = 1;

                    mod_name = (char *)&buf_mod_name;

                    /* Get name of imported module. */
                    org_mod_name = get_imp_mod_name(lx_exe_mod,mod_nr);
                    copy_pas_str(mod_name, org_mod_name);

                    if (min_rlc->nr_stype & NRCHAIN)
                    {
                        short *srcoff = (short *)
                            ((char *)min_rlc + 3 * 1 + mod_ord_size +
                            import_ord_size + additive_size);

                        for (i = 0; i < get_srcoff_cnt1_rlc(min_rlc); i++)
                        {
                            srcoff_cnt1 = srcoff[i];
                            io_log("mod_ord_size=%u, import_ord_size=%u, additive_size=%u\n",
                                   mod_ord_size, import_ord_size, additive_size);
                            io_log("srcoff[%i]=%x\n", i, srcoff[i]);

                            ixfModule->Fixups[*fixup_counter].flags = min_rlc->nr_stype & 0xf;
                            ixfModule->Fixups[*fixup_counter].SrcVmAddress = (void *)(vm_start_of_page + srcoff_cnt1);
                            ixfModule->Fixups[*fixup_counter].SrcAddress = (void *)(start_of_page + srcoff_cnt1 - addit);
                            ixfModule->Fixups[*fixup_counter].ImportEntry.FunctionName = NULL;
                            ixfModule->Fixups[*fixup_counter].ImportEntry.ModuleName = (char *)malloc(strlen(mod_name) + 1);
                            strcpy(ixfModule->Fixups[*fixup_counter].ImportEntry.ModuleName, mod_name);
                            ixfModule->Fixups[*fixup_counter].ImportEntry.Ordinal = (int)import_ord;
                            (*fixup_counter)++;
                        }
                    }
                    else
                    {
                        srcoff_cnt1 = get_srcoff_cnt1_rlc(min_rlc);

                        ixfModule->Fixups[*fixup_counter].flags = min_rlc->nr_stype & 0xf;
                        ixfModule->Fixups[*fixup_counter].SrcVmAddress = (void *)(vm_start_of_page + srcoff_cnt1);
                        ixfModule->Fixups[*fixup_counter].SrcAddress = (void *)(start_of_page + srcoff_cnt1 - addit);
                        ixfModule->Fixups[*fixup_counter].ImportEntry.FunctionName = NULL;
                        ixfModule->Fixups[*fixup_counter].ImportEntry.ModuleName = (char *)malloc(strlen(mod_name) + 1);
                        strcpy(ixfModule->Fixups[*fixup_counter].ImportEntry.ModuleName, mod_name);
                        ixfModule->Fixups[*fixup_counter].ImportEntry.Ordinal = (int)import_ord;
                        (*fixup_counter)++;
                    }
                }
                fixup_offset += get_reloc_size_rlc(min_rlc);
                break;

                case NRRNAM:
                {   /* Import by name */
                    int proc_name_off_size = 0;
                    int mod_ord_size = 0;

                    io_log("Import by name\n");
                    mod_nr = get_mod_ord1_rlc(min_rlc);
                    import_name_offs = get_imp_ord1_rlc(min_rlc);

                    if (min_rlc->nr_flags & 0x04) // additive present
                        additive_size = 2;

                    if (min_rlc->nr_flags & 0x20) // 32-bit additive field
                        additive_size = 4;

                    if (additive_size)
                        addit = get_additive_rlc(min_rlc);

                    if (min_rlc->nr_flags & 0x10) // 32-bit target offset flag
                        proc_name_off_size = 4;
                    else
                        proc_name_off_size = 2;

                    if (min_rlc->nr_flags & 0x40) // 16-bit object number/module ordinal flag
                        mod_ord_size = 2;
                    else
                        mod_ord_size = 1;

                    pas_imp_proc_name = get_imp_proc_name(lx_exe_mod, import_name_offs);
                    copy_pas_str(buf_import_name, pas_imp_proc_name);
                    import_name = (char *)&buf_import_name;
                    mod_name = (char *)&buf_mod_name;

                    /* Get name of imported module. */
                    org_mod_name = get_imp_mod_name(lx_exe_mod,mod_nr);
                    copy_pas_str(mod_name, org_mod_name);

                    if (min_rlc->nr_stype & NRCHAIN)
                    {
                        short *srcoff = (short *)
                            ((char *)min_rlc + 3 * 1 + mod_ord_size +
                            proc_name_off_size + additive_size);

                        for (i = 0; i < get_srcoff_cnt1_rlc(min_rlc); i++)
                        {
                            srcoff_cnt1 = srcoff[i];

                            ixfModule->Fixups[*fixup_counter].flags = min_rlc->nr_stype & 0xf;
                            ixfModule->Fixups[*fixup_counter].SrcVmAddress = (void *)(vm_start_of_page + srcoff_cnt1);
                            ixfModule->Fixups[*fixup_counter].SrcAddress = (void *)(start_of_page + srcoff_cnt1 - addit);
                            ixfModule->Fixups[*fixup_counter].ImportEntry.FunctionName = (char *)malloc(strlen(import_name) + 1);
                            strcpy(ixfModule->Fixups[*fixup_counter].ImportEntry.FunctionName, import_name);
                            ixfModule->Fixups[*fixup_counter].ImportEntry.ModuleName = (char *)malloc(strlen(mod_name) + 1);
                            strcpy(ixfModule->Fixups[*fixup_counter].ImportEntry.ModuleName, mod_name);
                            ixfModule->Fixups[*fixup_counter].ImportEntry.Ordinal = 0;
                            (*fixup_counter)++;
                        }
                    }
                    else
                    {
                        srcoff_cnt1 = get_srcoff_cnt1_rlc(min_rlc);

                        ixfModule->Fixups[*fixup_counter].flags = min_rlc->nr_stype & 0xf;
                        ixfModule->Fixups[*fixup_counter].SrcVmAddress = (void *)(vm_start_of_page + srcoff_cnt1);
                        ixfModule->Fixups[*fixup_counter].SrcAddress = (void *)(start_of_page + srcoff_cnt1 - addit);
                        ixfModule->Fixups[*fixup_counter].ImportEntry.FunctionName = (char *)malloc(strlen(import_name) + 1);
                        strcpy(ixfModule->Fixups[*fixup_counter].ImportEntry.FunctionName, import_name);
                        ixfModule->Fixups[*fixup_counter].ImportEntry.ModuleName = (char *)malloc(strlen(mod_name) + 1);
                        strcpy(ixfModule->Fixups[*fixup_counter].ImportEntry.ModuleName, mod_name);
                        ixfModule->Fixups[*fixup_counter].ImportEntry.Ordinal = 0;
                        (*fixup_counter)++;
                    }
                }
                fixup_offset += get_reloc_size_rlc(min_rlc);
                break;

                default:
                    io_log("Unsupported Fixup! SRC: 0x%x\n", fixup_source);
                    return 0; /* Is there any OS/2 error number for this? */
            } /* switch(fixup_source) */
        }     /* while(fixup_offset < pg_end_offs_fix) { */
    }

    return 1;
}

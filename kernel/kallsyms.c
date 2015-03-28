/* An example of using kallsyms data in a kernel debugger.

   Copyright 2000 Keith Owens <kaos@ocs.com.au> April 2000

   This file is part of the Linux modutils.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2 of the License, or (at your
   option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
  */

/*
   This code uses the list of all kernel and module symbols to :-

   * Find any non-stack symbol in a kernel or module.  Symbols do
     not have to be exported for debugging.

   * Convert an address to the module (or kernel) that owns it, the
     section it is in and the nearest symbol.  This finds all non-stack
     symbols, not just exported ones.

   You need modutils >= 2.3.11 and a kernel with the kallsyms patch
   which was compiled with CONFIG_KALLSYMS.
 */

#include <linux/elf.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/kallsyms.h>
#include <linux/kdb.h>

/* These external symbols are only set on kernels compiled with
 * CONFIG_KALLSYMS.
 */

extern const char __start___kallsyms[];
extern const char __stop___kallsyms[];

static struct module **kallsyms_module_list;

static void kallsyms_get_module_list(void)
{
	const struct kallsyms_header	*ka_hdr;
	const struct kallsyms_section	*ka_sec;
	const struct kallsyms_symbol	*ka_sym;
	const char			*ka_str;
	int i;
	const char *p;

	if (__start___kallsyms >= __stop___kallsyms)
		return;
	ka_hdr = (struct kallsyms_header *)__start___kallsyms;
	ka_sec = (struct kallsyms_section *)
		((char *)(ka_hdr) + ka_hdr->section_off);
	ka_sym = (struct kallsyms_symbol *)
		((char *)(ka_hdr) + ka_hdr->symbol_off);
	ka_str = 
		((char *)(ka_hdr) + ka_hdr->string_off);

	for (i = 0; i < ka_hdr->symbols; kallsyms_next_sym(ka_hdr, ka_sym), ++i) {
		p = ka_str + ka_sym->name_off;
		if (strcmp(p, "module_list") == 0) {
			if (ka_sym->symbol_addr)
				kallsyms_module_list = (struct module **)(ka_sym->symbol_addr);
			break;
		}
	}
}

static inline void kallsyms_do_first_time(void)
{
	static int first_time = 1;
	if (first_time)
		kallsyms_get_module_list();
	first_time = 0;
}

/* A symbol can appear in more than one module.  A token is used to
 * restart the scan at the next module, set the token to 0 for the
 * first scan of each symbol.
 */

int kallsyms_symbol_to_address(
	const char	 *name,		/* Name to lookup */
	unsigned long 	 *token,	/* Which module to start at */
	const char	**mod_name,	/* Set to module name */
	unsigned long 	 *mod_start,	/* Set to start address of module */
	unsigned long 	 *mod_end,	/* Set to end address of module */
	const char	**sec_name,	/* Set to section name */
	unsigned long 	 *sec_start,	/* Set to start address of section */
	unsigned long 	 *sec_end,	/* Set to end address of section */
	const char	**sym_name,	/* Set to full symbol name */
	unsigned long 	 *sym_start,	/* Set to start address of symbol */
	unsigned long 	 *sym_end	/* Set to end address of symbol */
	)
{
	const struct kallsyms_header	*ka_hdr = NULL;	/* stupid gcc */
	const struct kallsyms_section	*ka_sec;
	const struct kallsyms_symbol	*ka_sym = NULL;
	const char			*ka_str = NULL;
	const struct module *m;
	int i = 0, l;
	const char *p, *pt_R;
	char *p2;

	kallsyms_do_first_time();
	if (!kallsyms_module_list)
		return(0);

	/* Restart? */
	m = *kallsyms_module_list;
	if (token && *token) {
		for (; m; m = m->next)
			if ((unsigned long)m == *token)
				break;
		if (m)
			m = m->next;
	}

	for (; m; m = m->next) {
		if (!mod_member_present(m, kallsyms_start) || 
		    !mod_member_present(m, kallsyms_end) ||
		    m->kallsyms_start >= m->kallsyms_end)
			continue;
		ka_hdr = (struct kallsyms_header *)m->kallsyms_start;
		ka_sym = (struct kallsyms_symbol *)
			((char *)(ka_hdr) + ka_hdr->symbol_off);
		ka_str = 
			((char *)(ka_hdr) + ka_hdr->string_off);
		for (i = 0; i < ka_hdr->symbols; ++i, kallsyms_next_sym(ka_hdr, ka_sym)) {
			p = ka_str + ka_sym->name_off;
			if (strcmp(p, name) == 0)
				break;
			/* Unversioned requests match versioned names */
			if (!(pt_R = strstr(p, "_R")))
				continue;
			l = strlen(pt_R);
			if (l < 10)
				continue;	/* Not _R.*xxxxxxxx */
			(void)simple_strtoul(pt_R+l-8, &p2, 16);
			if (*p2)
				continue;	/* Not _R.*xxxxxxxx */
			if (strncmp(p, name, pt_R-p) == 0)
				break;	/* Match with version */
		}
		if (i < ka_hdr->symbols)
			break;
	}

	if (token)
		*token = (unsigned long)m;
	if (!m)
		return(0);	/* not found */

	ka_sec = (const struct kallsyms_section *)
		((char *)ka_hdr + ka_hdr->section_off + ka_sym->section_off);
	*mod_name = *(m->name) ? m->name : "kernel";
	*mod_start = ka_hdr->start;
	*mod_end = ka_hdr->end;
	*sec_name = ka_sec->name_off + ka_str;
	*sec_start = ka_sec->start;
	*sec_end = ka_sec->start + ka_sec->size;
	*sym_name = ka_sym->name_off + ka_str;
	*sym_start = ka_sym->symbol_addr;
	if (i < ka_hdr->symbols-1) {
		const struct kallsyms_symbol *ka_symn = ka_sym;
		kallsyms_next_sym(ka_hdr, ka_symn);
		*sym_end = ka_symn->symbol_addr;
	}
	else
		*sym_end = *sec_end;
	return(1);
}

int kallsyms_address_to_symbol(
	unsigned long	  address,	/* Address to lookup */
	const char	**mod_name,	/* Set to module name */
	unsigned long 	 *mod_start,	/* Set to start address of module */
	unsigned long 	 *mod_end,	/* Set to end address of module */
	const char	**sec_name,	/* Set to section name */
	unsigned long 	 *sec_start,	/* Set to start address of section */
	unsigned long 	 *sec_end,	/* Set to end address of section */
	const char	**sym_name,	/* Set to full symbol name */
	unsigned long 	 *sym_start,	/* Set to start address of symbol */
	unsigned long 	 *sym_end	/* Set to end address of symbol */
	)
{
	const struct kallsyms_header	*ka_hdr = NULL;	/* stupid gcc */
	const struct kallsyms_section	*ka_sec = NULL;
	const struct kallsyms_symbol	*ka_sym;
	const char			*ka_str;
	const struct module *m;
	int i;
	unsigned long end;

	kallsyms_do_first_time();
	if (!kallsyms_module_list)
		return(0);

	for (m = *kallsyms_module_list; m; m = m->next) {
		if (!mod_member_present(m, kallsyms_start) || 
		    !mod_member_present(m, kallsyms_end) ||
		    m->kallsyms_start >= m->kallsyms_end)
			continue;
		ka_hdr = (struct kallsyms_header *)m->kallsyms_start;
		ka_sec = (const struct kallsyms_section *)
			((char *)ka_hdr + ka_hdr->section_off);
		/* Is the address in any section in this module? */
		for (i = 0; i < ka_hdr->sections; ++i, kallsyms_next_sec(ka_hdr, ka_sec)) {
			if (ka_sec->start <= address &&
			    (ka_sec->start + ka_sec->size) > address)
				break;
		}
		if (i < ka_hdr->sections)
			break;	/* Found a matching section */
	}

	if (!m)
		return(0);	/* not found */

	ka_sym = (struct kallsyms_symbol *)
		((char *)(ka_hdr) + ka_hdr->symbol_off);
	ka_str = 
		((char *)(ka_hdr) + ka_hdr->string_off);
	*mod_name = *(m->name) ? m->name : "kernel";
	*mod_start = ka_hdr->start;
	*mod_end = ka_hdr->end;
	*sec_name = ka_sec->name_off + ka_str;
	*sec_start = ka_sec->start;
	*sec_end = ka_sec->start + ka_sec->size;
	*sym_name = *sec_name;		/* In case we find no matching symbol */
	*sym_start = *sec_start;
	*sym_end = *sec_end;

	for (i = 0; i < ka_hdr->symbols; ++i, kallsyms_next_sym(ka_hdr, ka_sym)) {
		if (ka_sym->symbol_addr > address)
			continue;
		if (i < ka_hdr->symbols-1) {
			const struct kallsyms_symbol *ka_symn = ka_sym;
			kallsyms_next_sym(ka_hdr, ka_symn);
			end = ka_symn->symbol_addr;
		}
		else
			end = *sec_end;
		if (end <= address)
			continue;
		if ((char *)ka_hdr + ka_hdr->section_off + ka_sym->section_off
		    != (char *)ka_sec)
			continue;	/* wrong section */
		*sym_name = ka_str + ka_sym->name_off;
		*sym_start = ka_sym->symbol_addr;
		*sym_end = end;
		break;
	}
	return(1);
}

/* List all sections in all modules.  The callback routine is invoked with
 * token, module name, section name, section start, section end, section flags.
 */
int kallsyms_sections(void *token,
		      int (*callback)(void *, const char *, const char *, ElfW(Addr), ElfW(Addr), ElfW(Word)))
{
	const struct kallsyms_header	*ka_hdr = NULL;	/* stupid gcc */
	const struct kallsyms_section	*ka_sec = NULL;
	const char			*ka_str;
	const struct module *m;
	int i;

	kallsyms_do_first_time();
	if (!kallsyms_module_list)
		return(0);

	for (m = *kallsyms_module_list; m; m = m->next) {
		if (!mod_member_present(m, kallsyms_start) || 
		    !mod_member_present(m, kallsyms_end) ||
		    m->kallsyms_start >= m->kallsyms_end)
			continue;
		ka_hdr = (struct kallsyms_header *)m->kallsyms_start;
		ka_sec = (const struct kallsyms_section *) ((char *)ka_hdr + ka_hdr->section_off);
		ka_str = ((char *)(ka_hdr) + ka_hdr->string_off);
		for (i = 0; i < ka_hdr->sections; ++i, kallsyms_next_sec(ka_hdr, ka_sec)) {
			if (callback(
				token,
				*(m->name) ? m->name : "kernel",
				ka_sec->name_off + ka_str,
				ka_sec->start,
				ka_sec->start + ka_sec->size,
				ka_sec->flags))
				return(0);
		}
	}
	return(1);
}


/* paramter prefix_name is a buffer provided by the caller, it must ends with '\0'. */
/* return the extra string together with the given prefix of a symbol name. */
/* return 0 means no prefix string is found. */
/* return >0 means prefix string is found. */
int kallsyms_symbol_complete(
	char	 *prefix_name	/* Prefix of a symbol name to lookup */
	)
{
	const struct kallsyms_header	*ka_hdr = NULL;	/* stupid gcc */
	const struct kallsyms_symbol	*ka_sym = NULL;
	const char			*ka_str = NULL;
	const struct module *m;
	int i = 0;
	int prefix_len=strlen(prefix_name);
	int cur_pos=0, last_pos=0;
	int find=0;
	int number=0;
	const char *p;

	kallsyms_do_first_time();
	if (!kallsyms_module_list)
		return(0);

	for (m = *kallsyms_module_list; m; m = m->next) {
		if (!mod_member_present(m, kallsyms_start) ||
		    !mod_member_present(m, kallsyms_end) ||
		    m->kallsyms_start >= m->kallsyms_end)
			continue;
		ka_hdr = (struct kallsyms_header *)m->kallsyms_start;
		ka_sym = (struct kallsyms_symbol *)
			((char *)(ka_hdr) + ka_hdr->symbol_off);
		ka_str =
			((char *)(ka_hdr) + ka_hdr->string_off);
		for (i = 0; i < ka_hdr->symbols; ++i, kallsyms_next_sym(ka_hdr, ka_sym)) {
			p = ka_str + ka_sym->name_off;
			if (strncmp(p, prefix_name,prefix_len) == 0) {
				++number;
				if (find == 0) {
					last_pos = strlen(p);
					strncpy(prefix_name, p, last_pos+1);
					find = 1;
				}
				else {
					for (cur_pos = prefix_len ; cur_pos < last_pos; cur_pos++) {
						if (*(p + cur_pos) == '\0'
							|| *(p + cur_pos) != prefix_name[cur_pos]) {
							last_pos = cur_pos;
							prefix_name[cur_pos] = '\0';
							break;
						}
					}
				}
			}
		}
	}

	return number;
}

/* paramter prefix_name is a buffer provided by the caller, it must ends with '\0'. */
/* parameter flag = 0 means search from the head, flag = 1 means continue search. */
/* return a symbol string which matches the given prefix. */
/* return 0 means no prefix string is found. */
/* return >0 means prefix string is found. */
int kallsyms_symbol_next(
	char	 *prefix_name,	/* Prefix of a symbol name to lookup */
	int flag			/* Indicate if search from the head */
	)
{
	const struct kallsyms_header	*ka_hdr = NULL;	/* stupid gcc */
	const char			*ka_str = NULL;
	static const struct kallsyms_symbol	*ka_sym;
	static const struct module *m;
	static int i;
	int prefix_len=strlen(prefix_name);
	const char *p;

	kallsyms_do_first_time();
	if (!kallsyms_module_list)
		return(0);

	if(!flag) {
		m = *kallsyms_module_list;
	}

	for (; m; m = m->next) {
		if (!mod_member_present(m, kallsyms_start) ||
		    !mod_member_present(m, kallsyms_end) ||
		    m->kallsyms_start >= m->kallsyms_end)
			continue;
		ka_hdr = (struct kallsyms_header *)m->kallsyms_start;
		if(!flag) {
			ka_sym = (struct kallsyms_symbol *)
				((char *)(ka_hdr) + ka_hdr->symbol_off);
			i = 0;
		}
		ka_str = ((char *)(ka_hdr) + ka_hdr->string_off);

		for (; i < ka_hdr->symbols; ++i, kallsyms_next_sym(ka_hdr, ka_sym)) {
			p = ka_str + ka_sym->name_off;
			if (strncmp(p, prefix_name,prefix_len) == 0) {
				strncpy(prefix_name, p, strlen(p)+1);
				++i;
				kallsyms_next_sym(ka_hdr, ka_sym);
				return 1;
			}
		}
	}

	return 0;
}

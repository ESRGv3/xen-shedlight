/*
 * xen/arch/arm/coloring.c
 *
 * Coloring support for ARM
 *
 * Copyright (C) 2019 Xilinx Inc.
 *
 * Authors:
 *    Luca Miccio <lucmiccio@gmail.com>
 *    Carlo Nonato <carlo.nonato@minervasys.tech>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <xen/errno.h>
#include <xen/guest_access.h>
#include <xen/keyhandler.h>
#include <xen/param.h>
#include <xen/types.h>

#include <asm/coloring.h>
#include <asm/processor.h>
#include <asm/sysregs.h>

/* By default Xen uses the lowestmost color */
#define XEN_DEFAULT_COLOR       0
#define XEN_DEFAULT_NUM_COLORS  1

/* Size of a LLC way */
static unsigned int llc_way_size;
/* Number of colors available in the LLC */
static unsigned int max_colors;
/* Mask to retrieve coloring relevant bits */
static uint64_t addr_col_mask;

#define addr_to_color(addr) (((addr) & addr_col_mask) >> PAGE_SHIFT)
#define addr_set_color(addr, color) (((addr) & ~addr_col_mask) \
                                     | ((color) << PAGE_SHIFT))

static unsigned int xen_colors[CONFIG_MAX_CACHE_COLORS];
static unsigned int xen_num_colors;

static unsigned int dom0_colors[CONFIG_MAX_CACHE_COLORS];
static unsigned int dom0_num_colors;

/*
 * Parse the coloring configuration given in the buf string, following the
 * syntax below.
 *
 * COLOR_CONFIGURATION ::= COLOR | RANGE,...,COLOR | RANGE
 * RANGE               ::= COLOR-COLOR
 *
 * Example: "0,2-6,15-16" represents the set of colors: 0,2,3,4,5,6,15,16.
 */
static int parse_color_config(const char *buf, unsigned int *colors,
                              unsigned int *num_colors)
{
    const char *s = buf;

    if ( !colors || !num_colors )
        return -EINVAL;

    while ( *s != '\0' )
    {
        if ( *s != ',' )
        {
            unsigned int color, start, end;

            start = simple_strtoul(s, &s, 0);

            if ( *s == '-' )    /* Range */
            {
                s++;
                end = simple_strtoul(s, &s, 0);
            }
            else                /* Single value */
                end = start;

            if ( start > end ||
                 *num_colors + end - start >= CONFIG_MAX_CACHE_COLORS )
                return -EINVAL;
            for ( color = start; color <= end; color++ )
                colors[(*num_colors)++] = color;
        }
        else
            s++;
    }

    return *s ? -EINVAL : 0;
}

size_param("llc-way-size", llc_way_size);

static int __init parse_xen_colors(const char *s)
{
    return parse_color_config(s, xen_colors, &xen_num_colors);
}
custom_param("xen-colors", parse_xen_colors);

static int __init parse_dom0_colors(const char *s)
{
    return parse_color_config(s, dom0_colors, &dom0_num_colors);
}
custom_param("dom0-colors", parse_dom0_colors);

/* Return the LLC way size by probing the hardware */
static unsigned int get_llc_way_size(void)
{
    register_t ccsidr_el1;
    register_t clidr_el1 = READ_SYSREG(CLIDR_EL1);
    register_t csselr_el1 = READ_SYSREG(CSSELR_EL1);
    register_t id_aa64mmfr2_el1 = READ_SYSREG(ID_AA64MMFR2_EL1);
    uint32_t ccsidr_numsets_shift = CCSIDR_NUMSETS_SHIFT;
    uint32_t ccsidr_numsets_mask = CCSIDR_NUMSETS_MASK;
    unsigned int n, line_size, num_sets;

    for ( n = CLIDR_CTYPEn_LEVELS;
          n != 0 && !((clidr_el1 >> CLIDR_CTYPEn_SHIFT(n)) & CLIDR_CTYPEn_MASK);
          n-- );

    if ( n == 0 )
        return 0;

    WRITE_SYSREG(((n - 1) & CCSELR_LEVEL_MASK) << CCSELR_LEVEL_SHIFT,
                 CSSELR_EL1);
    isb();

    ccsidr_el1 = READ_SYSREG(CCSIDR_EL1);

    /* Arm ARM: (Log2(Number of bytes in cache line)) - 4 */
    line_size = 1 << ((ccsidr_el1 & CCSIDR_LINESIZE_MASK) + 4);

    /* If FEAT_CCIDX is enabled, CCSIDR_EL1 has a different bit layout */
    if ( (id_aa64mmfr2_el1 >> ID_AA64MMFR2_CCIDX_SHIFT) & 0x7 )
    {
        ccsidr_numsets_shift = CCSIDR_NUMSETS_SHIFT_FEAT_CCIDX;
        ccsidr_numsets_mask = CCSIDR_NUMSETS_MASK_FEAT_CCIDX;
    }
    /* Arm ARM: (Number of sets in cache) - 1 */
    num_sets = ((ccsidr_el1 >> ccsidr_numsets_shift) & ccsidr_numsets_mask) + 1;

    printk(XENLOG_INFO "LLC found: L%u (line size: %u bytes, sets num: %u)\n",
           n, line_size, num_sets);

    /* Restore value in CSSELR_EL1 */
    WRITE_SYSREG(csselr_el1, CSSELR_EL1);
    isb();

    return line_size * num_sets;
}

static bool check_colors(unsigned int *colors, unsigned int num_colors)
{
    int i;

    if ( num_colors > max_colors )
        return false;

    for ( i = 0; i < num_colors; i++ )
        if ( colors[i] >= max_colors )
            return false;

    return true;
}

static unsigned int set_default_domain_colors(unsigned int *colors)
{
    unsigned int i;

    if ( !colors )
        return 0;

    for ( i = 0; i < max_colors; i++ )
        colors[i] = i;
    return max_colors;
}

static void print_colors(unsigned int *colors, unsigned int num_colors)
{
    unsigned int i;

    printk("[ ");
    for ( i = 0; i < num_colors; i++ )
        printk("%u ", colors[i]);
    printk("]\n");
}

static void dump_coloring_info(unsigned char key)
{
    printk("'%c' pressed -> dumping coloring general info\n", key);
    printk("LLC way size: %u KiB\n", llc_way_size >> 10);
    printk("Number of LLC colors supported: %u\n", max_colors);
    printk("Address color mask: 0x%lx\n", addr_col_mask);
    printk("Xen colors: ");
    print_colors(xen_colors, xen_num_colors);
}

bool __init coloring_init(void)
{
    if ( !llc_way_size && !(llc_way_size = get_llc_way_size()) )
    {
        printk(XENLOG_ERR
               "Probed LLC way size is 0 and no custom value provided\n");
        return false;
    }

    max_colors = llc_way_size / PAGE_SIZE;
    /* The maximum number of colors must be a power of 2 in order to correctly
       map colors to bits of an address. */
    ASSERT((max_colors & (max_colors - 1)) == 0);
    addr_col_mask = (max_colors - 1) << PAGE_SHIFT;

    if ( !xen_num_colors )
    {
        printk(XENLOG_WARNING
               "Xen color config not found. Using default color: %u\n",
               XEN_DEFAULT_COLOR);
        xen_colors[0] = XEN_DEFAULT_COLOR;
        xen_num_colors = XEN_DEFAULT_NUM_COLORS;
    }

    if ( !check_colors(xen_colors, xen_num_colors) )
    {
        printk(XENLOG_ERR "Bad color config for Xen\n");
        return false;
    }

    if ( !dom0_num_colors )
    {
        printk(XENLOG_WARNING
               "Dom0 color config not found. Using default (all colors)\n");
        dom0_num_colors = set_default_domain_colors(dom0_colors);
    }

    if ( !check_colors(dom0_colors, dom0_num_colors) )
    {
        printk(XENLOG_ERR "Bad color config for Dom0\n");
        return false;
    }

    register_keyhandler('K', dump_coloring_info, "dump coloring info", 1);

    return true;
}

int domain_coloring_init(struct domain *d,
                         const struct xen_arch_domainconfig *config)
{
    if ( is_domain_direct_mapped(d) )
    {
        printk(XENLOG_ERR
               "Can't enable coloring and directmap at the same time for %pd\n",
               d);
        return -EINVAL;
    }

    if ( is_hardware_domain(d) )
    {
        d->arch.colors = dom0_colors;
        d->arch.num_colors = dom0_num_colors;
    }
    else if ( config->num_colors == 0 )
    {
        printk(XENLOG_WARNING
               "Color config not found for %pd. Using default\n", d);
        d->arch.colors = xzalloc_array(unsigned int, max_colors);
        d->arch.num_colors = set_default_domain_colors(d->arch.colors);
    }
    else
    {
        d->arch.colors = xzalloc_array(unsigned int, config->num_colors);
        d->arch.num_colors = config->num_colors;
        if ( config->from_guest )
            copy_from_guest(d->arch.colors, config->colors, config->num_colors);
        else
        {
            memcpy(d->arch.colors, config->colors.p,
                   sizeof(unsigned int) * config->num_colors);
            xfree(config->colors.p);
        }
    }

    if ( !d->arch.colors )
    {
        printk(XENLOG_ERR "Colors allocation failed for %pd\n", d);
        return -ENOMEM;
    }

    if ( !check_colors(d->arch.colors, d->arch.num_colors) )
    {
        printk(XENLOG_ERR "Bad color config for %pd\n", d);
        return -EINVAL;
    }

    return 0;
}

void domain_coloring_free(struct domain *d)
{
    if ( !is_hardware_domain(d) )
        xfree(d->arch.colors);
}

void domain_dump_coloring_info(struct domain *d)
{
    printk("Domain %pd has %u colors: ", d, d->arch.num_colors);
    print_colors(d->arch.colors, d->arch.num_colors);
}

void prepare_color_domain_config(struct xen_arch_domainconfig *config,
                                 const char *colors_str)
{
    unsigned int num;

    config->colors.p = xzalloc_array(unsigned int, max_colors);
    if ( !config->colors.p )
        panic("Unable to allocate cache colors\n");

    if ( parse_color_config(colors_str, config->colors.p, &num) )
        panic("Error parsing the color configuration\n");
    config->num_colors = (uint16_t)num;
}

unsigned int page_to_color(struct page_info *pg)
{
    return addr_to_color(page_to_maddr(pg));
}

unsigned int get_max_colors(void)
{
    return max_colors;
}

paddr_t next_xen_colored(paddr_t phys)
{
    unsigned int i, color = addr_to_color(phys);

    for( i = 0; i < xen_num_colors; i++ )
    {
        if ( color == xen_colors[i] )
            return phys;
        else if ( color < xen_colors[i] )
            return addr_set_color(phys, xen_colors[i]);
    }

    /* Jump to next color space (llc_way_size bytes) and use the first color */
    return addr_set_color(phys + llc_way_size, xen_colors[0]);
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

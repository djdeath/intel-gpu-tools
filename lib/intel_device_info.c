#include "intel_chipset.h"
#include "i915_pciids.h"

#include <strings.h> /* ffs() */

#define BIT(x) (1<<(x))

#define DEFAULT_PARAMS \
	.simulator_id = -1

static const struct intel_device_info intel_generic_info = {
	DEFAULT_PARAMS,
	.gen = 0,
};

static const struct intel_device_info intel_i810_info = {
	DEFAULT_PARAMS,
	.gen = BIT(0),
	.is_whitney = true,
	.codename = "solano" /* 815 == "whitney" ? or vice versa? */
};

static const struct intel_device_info intel_i815_info = {
	DEFAULT_PARAMS,
	.gen = BIT(0),
	.is_whitney = true,
	.codename = "whitney"
};

static const struct intel_device_info intel_i830_info = {
	DEFAULT_PARAMS,
	.gen = BIT(1),
	.is_almador = true,
	.codename = "almador"
};
static const struct intel_device_info intel_i845_info = {
	DEFAULT_PARAMS,
	.gen = BIT(1),
	.is_brookdale = true,
	.codename = "brookdale"
};
static const struct intel_device_info intel_i855_info = {
	DEFAULT_PARAMS,
	.gen = BIT(1),
	.is_mobile = true,
	.is_montara = true,
	.codename = "montara"
};
static const struct intel_device_info intel_i865_info = {
	DEFAULT_PARAMS,
	.gen = BIT(1),
	.is_springdale = true,
	.codename = "spingdale"
};

static const struct intel_device_info intel_i915_info = {
	DEFAULT_PARAMS,
	.gen = BIT(2),
	.is_grantsdale = true,
	.codename = "grantsdale"
};
static const struct intel_device_info intel_i915m_info = {
	DEFAULT_PARAMS,
	.gen = BIT(2),
	.is_mobile = true,
	.is_alviso = true,
	.codename = "alviso"
};
static const struct intel_device_info intel_i945_info = {
	DEFAULT_PARAMS,
	.gen = BIT(2),
	.is_lakeport = true,
	.codename = "lakeport"
};
static const struct intel_device_info intel_i945m_info = {
	DEFAULT_PARAMS,
	.gen = BIT(2),
	.is_mobile = true,
	.is_calistoga = true,
	.codename = "calistoga"
};

static const struct intel_device_info intel_g33_info = {
	DEFAULT_PARAMS,
	.gen = BIT(2),
	.is_bearlake = true,
	.codename = "bearlake"
};
static const struct intel_device_info intel_pineview_info = {
	DEFAULT_PARAMS,
	.gen = BIT(2),
	.is_mobile = true,
	.is_pineview = true,
	.codename = "pineview"
};

static const struct intel_device_info intel_i965_info = {
	DEFAULT_PARAMS,
	.gen = BIT(3),
	.is_broadwater = true,
	.codename = "broadwater"
};

static const struct intel_device_info intel_i965m_info = {
	DEFAULT_PARAMS,
	.gen = BIT(3),
	.is_mobile = true,
	.is_crestline = true,
	.codename = "crestline"
};

static const struct intel_device_info intel_g45_info = {
	DEFAULT_PARAMS,
	.gen = BIT(3),
	.is_eaglelake = true,
	.codename = "eaglelake"
};
static const struct intel_device_info intel_gm45_info = {
	DEFAULT_PARAMS,
	.gen = BIT(3),
	.is_mobile = true,
	.is_cantiga = true,
	.codename = "cantiga"
};

static const struct intel_device_info intel_ironlake_info = {
	DEFAULT_PARAMS,
	.gen = BIT(4),
	.is_ironlake = true,
	.codename = "ironlake" /* clarkdale? */
};
static const struct intel_device_info intel_ironlake_m_info = {
	DEFAULT_PARAMS,
	.gen = BIT(4),
	.is_mobile = true,
	.is_arrandale = true,
	.codename = "arrandale"
};

#define SNB_PARAMS \
	DEFAULT_PARAMS, \
	.gen = BIT(5), \
	.is_sandybridge = true, \
	.codename = "sandybridge"

static const struct intel_device_info intel_sandybridge_info = {
	SNB_PARAMS,
};
static const struct intel_device_info intel_sandybridge_m_info = {
	SNB_PARAMS,
	.is_mobile = true,
};

#define IVB_PARAMS \
	DEFAULT_PARAMS, \
	.gen = BIT(6), \
	.is_ivybridge = true, \
	.codename = "ivybridge", \
	.simulator_id = 7

static const struct intel_device_info intel_ivybridge_info = {
	IVB_PARAMS,
};
static const struct intel_device_info intel_ivybridge_m_info = {
	IVB_PARAMS,
	.is_mobile = true,
};

static const struct intel_device_info intel_valleyview_info = {
	DEFAULT_PARAMS,
	.gen = BIT(6),
	.is_valleyview = true,
	.codename = "valleyview",
	.simulator_id = 10,
};

#define HASWELL_FIELDS \
	DEFAULT_PARAMS, \
	.gen = BIT(6), \
	.is_haswell = true, \
	.codename = "haswell", \
	.simulator_id = 9

static const struct intel_device_info intel_haswell_gt1_info = {
	HASWELL_FIELDS,
	.gt = 1,
};

static const struct intel_device_info intel_haswell_gt2_info = {
	HASWELL_FIELDS,
	.gt = 2,
};

static const struct intel_device_info intel_haswell_gt3_info = {
	HASWELL_FIELDS,
	.gt = 3,
};

#define BROADWELL_FIELDS \
	DEFAULT_PARAMS, \
	.gen = BIT(7), \
	.is_broadwell = true, \
	.codename = "broadwell", \
	.simulator_id = 11

static const struct intel_device_info intel_broadwell_gt1_info = {
	BROADWELL_FIELDS,
	.gt = 1,
};

static const struct intel_device_info intel_broadwell_gt2_info = {
	BROADWELL_FIELDS,
	.gt = 2,
};

static const struct intel_device_info intel_broadwell_gt3_info = {
	BROADWELL_FIELDS,
	.gt = 3,
};

static const struct intel_device_info intel_broadwell_unknown_info = {
	BROADWELL_FIELDS,
};

static const struct intel_device_info intel_cherryview_info = {
	DEFAULT_PARAMS,
	.gen = BIT(7),
	.is_cherryview = true,
	.codename = "cherryview",
	.simulator_id = 13,
};

#define SKYLAKE_FIELDS \
	DEFAULT_PARAMS, \
	.gen = BIT(8), \
	.codename = "skylake", \
	.is_skylake = true, \
	.simulator_id = 12

static const struct intel_device_info intel_skylake_gt1_info = {
	SKYLAKE_FIELDS,
	.gt = 1,
};

static const struct intel_device_info intel_skylake_gt2_info = {
	SKYLAKE_FIELDS,
	.gt = 2,
};

static const struct intel_device_info intel_skylake_gt3_info = {
	SKYLAKE_FIELDS,
	.gt = 3,
};

static const struct intel_device_info intel_skylake_gt4_info = {
	SKYLAKE_FIELDS,
	.gt = 4,
};

static const struct intel_device_info intel_broxton_info = {
	DEFAULT_PARAMS,
	.gen = BIT(8),
	.is_broxton = true,
	.codename = "broxton",
	.simulator_id = 14,
};

#define KABYLAKE_FIELDS \
	DEFAULT_PARAMS, \
	.gen = BIT(8), \
	.is_kabylake = true, \
	.codename = "kabylake", \
	.simulator_id = 16

static const struct intel_device_info intel_kabylake_gt1_info = {
	KABYLAKE_FIELDS,
	.gt = 1,
};

static const struct intel_device_info intel_kabylake_gt2_info = {
	KABYLAKE_FIELDS,
	.gt = 2,
};

static const struct intel_device_info intel_kabylake_gt3_info = {
	KABYLAKE_FIELDS,
	.gt = 3,
};

static const struct intel_device_info intel_kabylake_gt4_info = {
	KABYLAKE_FIELDS,
	.gt = 4,
};

static const struct intel_device_info intel_geminilake_info = {
	DEFAULT_PARAMS,
	.gen = BIT(8),
	.is_geminilake = true,
	.codename = "geminilake",
	.simulator_id = 17,
};

#define COFFEELAKE_FIELDS \
	DEFAULT_PARAMS, \
	.gen = BIT(8), \
	.is_coffeelake = true, \
	.codename = "coffeelake", \
	.simulator_id = 24

static const struct intel_device_info intel_coffeelake_gt1_info = {
	COFFEELAKE_FIELDS,
	.gt = 1,
};

static const struct intel_device_info intel_coffeelake_gt2_info = {
	COFFEELAKE_FIELDS,
	.gt = 2,
};

static const struct intel_device_info intel_coffeelake_gt3_info = {
	COFFEELAKE_FIELDS,
	.gt = 3,
};

static const struct intel_device_info intel_cannonlake_info = {
	DEFAULT_PARAMS,
	.gen = BIT(9),
	.is_cannonlake = true,
	.codename = "cannonlake",
	.simulator_id = 15,
};

static const struct intel_device_info intel_icelake_info = {
	DEFAULT_PARAMS,
	.gen = BIT(10),
	.is_icelake = true,
	.codename = "icelake",
	.simulator_id = 18,
};

static const struct pci_id_match intel_device_match[] = {
	INTEL_I810_IDS(&intel_i810_info),
	INTEL_I815_IDS(&intel_i815_info),

	INTEL_I830_IDS(&intel_i830_info),
	INTEL_I845G_IDS(&intel_i845_info),
	INTEL_I85X_IDS(&intel_i855_info),
	INTEL_I865G_IDS(&intel_i865_info),

	INTEL_I915G_IDS(&intel_i915_info),
	INTEL_I915GM_IDS(&intel_i915m_info),
	INTEL_I945G_IDS(&intel_i945_info),
	INTEL_I945GM_IDS(&intel_i945m_info),

	INTEL_G33_IDS(&intel_g33_info),
	INTEL_PINEVIEW_IDS(&intel_pineview_info),

	INTEL_I965G_IDS(&intel_i965_info),
	INTEL_I965GM_IDS(&intel_i965m_info),

	INTEL_G45_IDS(&intel_g45_info),
	INTEL_GM45_IDS(&intel_gm45_info),

	INTEL_IRONLAKE_D_IDS(&intel_ironlake_info),
	INTEL_IRONLAKE_M_IDS(&intel_ironlake_m_info),

	INTEL_SNB_D_IDS(&intel_sandybridge_info),
	INTEL_SNB_M_IDS(&intel_sandybridge_m_info),

	INTEL_IVB_D_IDS(&intel_ivybridge_info),
	INTEL_IVB_M_IDS(&intel_ivybridge_m_info),

	INTEL_HSW_GT1_IDS(&intel_haswell_gt1_info),
	INTEL_HSW_GT2_IDS(&intel_haswell_gt2_info),
	INTEL_HSW_GT3_IDS(&intel_haswell_gt3_info),

	INTEL_VLV_IDS(&intel_valleyview_info),

	INTEL_BDW_GT1_IDS(&intel_broadwell_gt1_info),
	INTEL_BDW_GT2_IDS(&intel_broadwell_gt2_info),
	INTEL_BDW_GT3_IDS(&intel_broadwell_gt3_info),
	INTEL_BDW_RSVD_IDS(&intel_broadwell_unknown_info),

	INTEL_CHV_IDS(&intel_cherryview_info),

	INTEL_SKL_GT1_IDS(&intel_skylake_gt1_info),
	INTEL_SKL_GT2_IDS(&intel_skylake_gt2_info),
	INTEL_SKL_GT3_IDS(&intel_skylake_gt3_info),
	INTEL_SKL_GT4_IDS(&intel_skylake_gt4_info),

	INTEL_BXT_IDS(&intel_broxton_info),

	INTEL_KBL_GT1_IDS(&intel_kabylake_gt1_info),
	INTEL_KBL_GT2_IDS(&intel_kabylake_gt2_info),
	INTEL_KBL_GT3_IDS(&intel_kabylake_gt3_info),
	INTEL_KBL_GT4_IDS(&intel_kabylake_gt4_info),

	INTEL_GLK_IDS(&intel_geminilake_info),

	INTEL_CFL_S_GT1_IDS(&intel_coffeelake_gt1_info),
	INTEL_CFL_U_GT1_IDS(&intel_coffeelake_gt1_info),
	INTEL_CFL_S_GT2_IDS(&intel_coffeelake_gt2_info),
	INTEL_CFL_H_GT2_IDS(&intel_coffeelake_gt2_info),
	INTEL_CFL_U_GT2_IDS(&intel_coffeelake_gt2_info),
	INTEL_CFL_U_GT3_IDS(&intel_coffeelake_gt3_info),

	INTEL_CNL_IDS(&intel_cannonlake_info),

	INTEL_ICL_11_IDS(&intel_icelake_info),

	INTEL_VGA_DEVICE(PCI_MATCH_ANY, &intel_generic_info),
};

/**
 * intel_get_device_info:
 * @devid: pci device id
 *
 * Looks up the Intel GFX device info for the given device id.
 *
 * Returns:
 * The associated intel_get_device_info
 */
const struct intel_device_info *intel_get_device_info(uint16_t devid)
{
	static const struct intel_device_info *cache = &intel_generic_info;
	static uint16_t cached_devid;
	int i;

	if (cached_devid == devid)
		goto out;

	/* XXX Presort table and bsearch! */
	for (i = 0; intel_device_match[i].device_id != PCI_MATCH_ANY; i++) {
		if (devid == intel_device_match[i].device_id)
			break;
	}

	cached_devid = devid;
	cache = (void *)intel_device_match[i].match_data;

out:
	return cache;
}

/**
 * intel_gen:
 * @devid: pci device id
 *
 * Computes the Intel GFX generation for the given device id.
 *
 * Returns:
 * The GFX generation on successful lookup, 0 on failure.
 */
unsigned intel_gen(uint16_t devid)
{
	return ffs(intel_get_device_info(devid)->gen);
}

/**
 * intel_gt:
 * @devid: pci device id
 *
 * Computes the Intel GFX GT size for the given device id.
 *
 * Returns:
 * The GT size.
 */
unsigned intel_gt(uint16_t devid)
{
	unsigned mask = intel_gen(devid);

	if (mask >= 8)
		mask = 0xf;
	else if (mask >= 6)
		mask = 0x3;
	else
		mask = 0;

	return (devid >> 4) & mask;
}

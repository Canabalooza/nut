/* idowell-hid.c - subdriver to monitor iDowell USB/HID devices with NUT
 *
 *  Copyright (C)
 *  2003 - 2009	Arnaud Quette <ArnaudQuette@Eaton.com>
 *  2005 - 2006	Peter Selinger <selinger@users.sourceforge.net>
 *  2008 - 2009	Arjen de Korte <adkorte-guest@alioth.debian.org>
 *
 *  Note: this subdriver was initially generated as a "stub" by the
 *  gen-usbhid-subdriver script. It must be customized.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "config.h" /* must be first */

#include "usbhid-ups.h"
#include "idowell-hid.h"
#include "main.h"	/* for getval() */
#include "usb-common.h"
#include "hidparser.h" /* for FindObject_with_ID_Node() */

#define IDOWELL_HID_VERSION	"iDowell HID 0.26"
/* v0.26 GoldenMate: read the live status block that this firmware only
 *       exposes through undeclared HID reports, through the
 *       idowell_hid_aux_vendor_block hook set */
/* v0.25 GoldenMate: shrink the oversized RunTimeToEmpty copies whose high
 *       bytes carry PresentStatus state (battery.runtime 599940 =
 *       (0x09 << 16) + 10116), see idowell_fix_report_desc() */
/* FIXME: experimental flag to be put in upsdrv_info */
/* v0.24 GoldenMate: restore the Power Device page on the Good and
 *       ShutdownImminent PresentStatus bits, see idowell_fix_status_usage() */
/* v0.21 GoldenMate LiFePO4 packs reuse the shared Phoenixtec VID (0x06da):
 *       claim only those (by -BMS-/Smart-Battery firmware strings) and defer
 *       everything else on that VID to liebert-hid / mge-hid */
/* v0.20 Goldenmate also uses the same vendorID and productID so added additional HID2NUT lookup values */

/* iDowell, Goldenmate */
#define IDOWELL_VENDORID	0x075d

/* Phoenixtec Power Co., Ltd; this VID is shared with liebert-hid (the default
 * sink for 0x06da) and mge-hid (AEG PROTECT NAS). GoldenMate 1000VA/800W
 * LiFePO4 packs reuse 0x06da:0xffff with the same iDowell-style -BMS- firmware
 * and HID descriptor as 0x075d:0x0300, so the table match is gated by device
 * strings in idowell_claim() to avoid hijacking the other 0x06da devices. */
#define PHOENIXTEC_VENDORID	0x06da

/* USB IDs device table */
static usb_device_id_t idowell_usb_device_table[] = {
	/* iDowell, Goldenmate */
	{ USB_DEVICE(IDOWELL_VENDORID, 0x0300), NULL },
	/* GoldenMate 1000VA/800W LiFePO4; claim gated by strings, see idowell_claim() */
	{ USB_DEVICE(PHOENIXTEC_VENDORID, 0xffff), NULL },

	/* Terminating entry */
	{ 0, 0, NULL }
};

/* --------------------------------------------------------------- */
/*      Vendor-specific usage table */
/* --------------------------------------------------------------- */

/* IDOWELL usage table */
static usage_lkp_t idowell_usage_lkp[] = {
	{  NULL, 0 }
};

static usage_tables_t idowell_utab[] = {
	idowell_usage_lkp,
	hid_usage_lkp,
	NULL,
};

/* --------------------------------------------------------------- */
/* HID2NUT lookup table                                            */
/* --------------------------------------------------------------- */

static hid_info_t idowell_hid2nut[] = {
#if WITH_UNMAPPED_DATA_POINTS || (defined DEBUG)
	{ "unmapped.ups.flow.[4].flowid", 0, 0, "UPS.Flow.[4].FlowID", NULL, "%.0f", 0, NULL },
	{ "unmapped.ups.powerconverter.output.outputid", 0, 0, "UPS.PowerConverter.Output.OutputID", NULL, "%.0f", 0, NULL },
	{ "unmapped.ups.powerconverter.powerconverterid", 0, 0, "UPS.PowerConverter.PowerConverterID", NULL, "%.0f", 0, NULL },
	{ "unmapped.ups.powersummary.batterypresent", 0, 0, "UPS.PowerSummary.BatteryPresent", NULL, "%.0f", 0, NULL },
	{ "unmapped.ups.powersummary.capacitygranularity1", 0, 0, "UPS.PowerSummary.CapacityGranularity1", NULL, "%.0f", 0, NULL },
	{ "unmapped.ups.powersummary.capacitymode", 0, 0, "UPS.PowerSummary.CapacityMode", NULL, "%.0f", 0, NULL },
	{ "unmapped.ups.powersummary.flowid", 0, 0, "UPS.PowerSummary.FlowID", NULL, "%.0f", 0, NULL },
	{ "unmapped.ups.powersummary.powersummaryid", 0, 0, "UPS.PowerSummary.PowerSummaryID", NULL, "%.0f", 0, NULL },
	{ "unmapped.ups.powersummary.presentstatus.atratetimetoempty", 0, 0, "UPS.PowerSummary.PresentStatus.AtRateTimeToEmpty", NULL, "%.0f", 0, NULL },
	{ "unmapped.ups.powersummary.presentstatus.averagetimetoempty", 0, 0, "UPS.PowerSummary.PresentStatus.AverageTimeToEmpty", NULL, "%.0f", 0, NULL },
	{ "unmapped.ups.powersummary.presentstatus.undefined", 0, 0, "UPS.PowerSummary.PresentStatus.Undefined", NULL, "%.0f", 0, NULL },
	{ "unmapped.ups.powersummary.rechargeable", 0, 0, "UPS.PowerSummary.Rechargeable", NULL, "%.0f", HU_FLAG_STATIC, NULL }, /* Read only */
#endif	/* if WITH_UNMAPPED_DATA_POINTS || DEBUG */

	{ "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.ACPresent", NULL, NULL, HU_FLAG_QUICK_POLL, online_info },
	{ "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.BatteryPresent", NULL, NULL, 0, nobattery_info },
	{ "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.BelowRemainingCapacityLimit", NULL, NULL, HU_FLAG_QUICK_POLL, lowbatt_info },
	{ "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.Charging", NULL, NULL, HU_FLAG_QUICK_POLL, charging_info },
	{ "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.CommunicationLost", NULL, NULL, 0, commfault_info },
	{ "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.Discharging", NULL, NULL, HU_FLAG_QUICK_POLL, discharging_info },
	{ "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.Good", NULL, NULL, 0, off_info },
	{ "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.InternalFailure", NULL, NULL, 0, commfault_info },
	{ "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.NeedReplacement", NULL, NULL, 0, replacebatt_info },
	{ "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.Overload", NULL, NULL, 0, overload_info },
	{ "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.ShutdownImminent", NULL, NULL, 0, shutdownimm_info },

	/* device page */
	{ "device.mfr", 0, 0, "UPS.PowerSummary.iManufacturer", NULL, "%s", HU_FLAG_STATIC, stringid_conversion }, /* Read only */
	{ "device.model", 0, 0, "UPS.PowerSummary.iProduct", NULL, "%s", HU_FLAG_STATIC, stringid_conversion }, /* Read only */
	{ "device.serial", 0, 0, "UPS.PowerSummary.iSerialNumber", NULL, "%s", HU_FLAG_STATIC, stringid_conversion }, /* Read only */

	/* battery page */
	{ "battery.capacity.design", 0, 0, "UPS.PowerSummary.DesignCapacity", NULL, "%.0f", HU_FLAG_STATIC, NULL }, /* Read only */
	{ "battery.charge", 0, 0, "UPS.PowerSummary.RemainingCapacity", NULL, "%.0f", 0, NULL },
	{ "battery.charge.low", 0, 0, "UPS.PowerSummary.RemainingCapacityLimit", NULL, "%.0f", HU_FLAG_STATIC , NULL }, /* Read only */
	{ "battery.charge.low", 0, 0, "UPS.PowerSummary.WarningCapacityLimit", NULL, "%.0f", HU_FLAG_STATIC, NULL }, /* Read only */
	{ "battery.capacity.full", 0, 0, "UPS.PowerSummary.FullChargeCapacity", NULL, "%.0f", 0, NULL },
	{ "battery.runtime", 0, 0, "UPS.PowerSummary.RunTimeToEmpty", NULL, "%.0f", 0, NULL },
	{ "battery.type", 0, 0, "UPS.PowerSummary.iDeviceChemistry", NULL, "%s", HU_FLAG_STATIC, stringid_conversion }, /* Read only */

	/* UPS page */
	{ "ups.delay.start", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.PowerSummary.DelayBeforeStartup", NULL, DEFAULT_ONDELAY, HU_FLAG_ABSENT, NULL},
	{ "ups.delay.shutdown", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.PowerSummary.DelayBeforeShutdown", NULL, DEFAULT_OFFDELAY, HU_FLAG_ABSENT, NULL},
	{ "ups.timer.start", 0, 0, "UPS.PowerSummary.DelayBeforeStartup", NULL, "%.0f", HU_FLAG_QUICK_POLL, NULL},
	{ "ups.timer.shutdown", 0, 0, "UPS.PowerSummary.DelayBeforeShutdown", NULL, "%.0f", HU_FLAG_QUICK_POLL, NULL},
	{ "ups.load", 0, 0, "UPS.PowerSummary.PercentLoad", NULL, "%.0f", 0, NULL },
	{ "ups.power.nominal", 0, 0, "UPS.Flow.[4].ConfigApparentPower", NULL, "%.0f", HU_FLAG_STATIC, NULL },

	/* input page */
	{ "input.transfer.high", 0, 0, "UPS.PowerConverter.Output.HighVoltageTransfer", NULL, "%.0f", HU_FLAG_STATIC, NULL },
	{ "input.transfer.low", 0, 0, "UPS.PowerConverter.Output.LowVoltageTransfer", NULL, "%.0f", HU_FLAG_STATIC, NULL },

	/* output page */
	{ "output.voltage", 0, 0, "UPS.PowerConverter.Output.Voltage", NULL, "%.1f", 0, NULL },
	{ "battery.voltage", 0, 0, "UPS.PowerSummary.Voltage", NULL, "%.1f", 0, NULL },
	{ "output.voltage.nominal", 0, 0, "UPS.Flow.[4].ConfigVoltage", NULL, "%.0f", HU_FLAG_STATIC, NULL },
	{ "battery.voltage.nominal", 0, 0, "UPS.PowerSummary.ConfigVoltage", NULL, "%.1f", HU_FLAG_STATIC, NULL },
	{ "output.frequency.nominal", 0, 0, "UPS.Flow.[4].ConfigFrequency", NULL, "%.0f", HU_FLAG_STATIC, NULL },

	/* instant commands */
	{ "load.off.delay", 0, 0, "UPS.PowerSummary.DelayBeforeShutdown", NULL, DEFAULT_OFFDELAY, HU_TYPE_CMD, NULL },
	{ "load.on.delay", 0, 0, "UPS.PowerSummary.DelayBeforeStartup", NULL, DEFAULT_ONDELAY, HU_TYPE_CMD, NULL },
	{ "shutdown.stop", 0, 0, "UPS.PowerSummary.DelayBeforeShutdown", NULL, "-1", HU_TYPE_CMD, NULL },

	/* end of structure. */
	{ NULL, 0, 0, NULL, NULL, NULL, 0, NULL }
};

static const char *idowell_format_model(HIDDevice_t *hd) {
	return hd->Product;
}

static const char *idowell_format_mfr(HIDDevice_t *hd) {
	return hd->Vendor ? hd->Vendor : "iDowell";
}

static const char *idowell_format_serial(HIDDevice_t *hd) {
	return hd->Serial;
}

/* The Phoenixtec vendor ID (0x06da) is shared between several subdrivers:
 * liebert-hid is the default sink, mge-hid grabs AEG PROTECT NAS, and we want
 * only the GoldenMate LiFePO4 packs. They carry the same iDowell-style BMS
 * firmware as the 0x075d:0x0300 device, reporting manufacturer "-BMS-" and
 * product "Smart-Battery". Match on those strings so the other 0x06da devices
 * fall through to liebert-hid / mge-hid. */
static int idowell_is_goldenmate(HIDDevice_t *hd)
{
	if (hd->Vendor && strstr(hd->Vendor, "BMS")) {
		return 1;
	}
	if (hd->Product && strstr(hd->Product, "Smart-Battery")) {
		return 1;
	}
	return 0;
}

/* this function allows the subdriver to "claim" a device: return 1 if
 * the device is supported by this subdriver, else 0. */
/* the device this subdriver won: VendorID/ProductID are needed by the
 * vendor block quirk outside of claim() and fix_report_desc() */
static HIDDevice_t		*idowell_claimed_dev;

static int idowell_claim(HIDDevice_t *hd)
{
	int status = is_usb_device_supported(idowell_usb_device_table, hd);

	switch (status)
	{
	case POSSIBLY_SUPPORTED:
		/* by default, reject, unless the productid option is given */
		if (getval("productid")) {
			return 1;
		}
		possibly_supported("iDowell", hd);
		return 0;

	case SUPPORTED:
		/* On the shared Phoenixtec VID, only claim GoldenMate; let
		 * liebert-hid (default sink) and mge-hid (AEG) handle the rest. */
		if (hd->VendorID == PHOENIXTEC_VENDORID && !idowell_is_goldenmate(hd)) {
			return 0;
		}
		idowell_claimed_dev = hd;
		return 1;

	case NOT_SUPPORTED:
	default:
		return 0;
	}
}

/* idowell_fix_report_desc: correct a firmware bug in GoldenMate LiFePO4 packs.
 *
 * These devices declare the same data points twice: once as Feature items in
 * ReportID 0x01 (with sane Logical Maximum values) and again as Input items in
 * ReportID 0x02. In ReportID 0x02 the firmware encodes the 4-byte Logical
 * Maximum for UPS.PowerSummary.RunTimeToEmpty with the bytes in the wrong
 * order: it emits "27 ff ff ff fe" (0xFEFFFFFF) where 0xFFFFFFFE was intended.
 *
 * 0xFEFFFFFF does not fit a signed 32-bit long, so on LLP64 platforms (Windows,
 * where long is 32-bit) the generic "LogMax < LogMin" recovery in HIDParse()
 * stores it back as the negative value -16777217. Per the HID spec, Logical
 * values persist in global state, so the following UPS.PowerSummary.Voltage
 * Input item inherits the same broken maximum. Both readings then come out as
 * nonsense (battery.runtime = -16777217, battery.voltage = -167772.2) while the
 * ReportID 0x01 Feature copies of the very same usages read correctly.
 *
 * Rather than invent limits, we copy the values this device itself declares for
 * the same usages in ReportID 0x01: 0x75FFFFFF for RunTimeToEmpty (32-bit) and
 * 65535 for Voltage (16-bit).
 */
#define IDOWELL_RUNTIME_LOGMAX	0x75FFFFFFL
#define IDOWELL_VOLTAGE_LOGMAX	65535L

/* idowell_fix_status_usage: correct a second firmware bug in the same packs.
 *
 * Feature report 0x01 declares seven 1-bit PresentStatus flags, but the
 * firmware sets Usage Page 0x85 (Battery System) for ACPresent and never
 * switches back to 0x84 (Power Device) for two of the flags:
 *
 *   05 85  09 d0 09 44 09 45 09 42  09 61  09 d1  09 69
 *          ACPresent Charging       ^^^^^  Battery ^^^^^
 *          Discharging BelowRCL     0x61   Present 0x69
 *
 * On the Battery System page 0x61 and 0x69 are AtRateTimeToEmpty and
 * AverageTimeToEmpty, which are run-time counters and make no sense as
 * single-bit status flags. On the Power Device page the same usages are
 * Good and ShutdownImminent, which is clearly what was intended: on a
 * healthy 06da:ffff unit they read 1 and 0 respectively both on mains
 * and on battery, while ACPresent/Discharging flip as expected.
 *
 * Move just those two usages back to the Power Device page so the existing
 * PresentStatus.Good and PresentStatus.ShutdownImminent mappings apply.
 * The fix is narrowly scoped: report 0x01, Feature, 1-bit, directly under
 * PresentStatus; anything else is left alone.
 */
static int idowell_fix_status_usage(HIDDesc_t *pDesc_arg,
	HIDNode_t wrong_usage, HIDNode_t right_usage)
{
	HIDData_t	*pData;
	HIDPath_t	*pPath;

	if (!(pData = FindObject_with_ID_Node(pDesc_arg, 0x01, wrong_usage))) {
		return 0;
	}

	pPath = &pData->Path;
	if (pData->Type != ITEM_FEATURE || pData->Size != 1 || pPath->Size < 2
	 || pPath->Node[pPath->Size - 2] != USAGE_POW_PRESENT_STATUS
	) {
		upsdebugx(4, "NOT fixing Report Descriptor: ReportID 0x01 "
			"usage 0x%08x is not a 1-bit PresentStatus Feature flag",
			(unsigned int)wrong_usage);
		return 0;
	}

	pPath->Node[pPath->Size - 1] = right_usage;
	upsdebugx(3, "Fixing Report Descriptor: set ReportID 0x01 "
		"PresentStatus usage 0x%08x => 0x%08x",
		(unsigned int)wrong_usage, (unsigned int)right_usage);

	return 1;
}

static int idowell_fix_report_desc(HIDDevice_t *pDev, HIDDesc_t *pDesc_arg) {
	HIDData_t	*pData;
	int	retval = 0;

	int	vendorID = pDev->VendorID;
	int	productID = pDev->ProductID;

	if (!((vendorID == PHOENIXTEC_VENDORID && productID == 0xffff)
	   || (vendorID == IDOWELL_VENDORID && productID == 0x0300))) {
		upsdebugx(3, "NOT Attempting Report Descriptor fix for UPS: "
			"Vendor: %04x, Product: %04x (vendor/product not matched)",
			(unsigned int)vendorID, (unsigned int)productID);
		return 0;
	}

	if (disable_fix_report_desc) {
		upsdebugx(3, "NOT Attempting Report Descriptor fix for UPS: "
			"Vendor: %04x, Product: %04x "
			"(got disable_fix_report_desc in config)",
			(unsigned int)vendorID, (unsigned int)productID);
		return 0;
	}

	upsdebugx(3, "Attempting Report Descriptor fix for UPS: "
		"Vendor: %04x, Product: %04x",
		(unsigned int)vendorID, (unsigned int)productID);

	/* Only touch items whose maximum is demonstrably broken (below the
	 * minimum), so a firmware revision that encodes this correctly is
	 * left alone. */
	if ((pData = FindObject_with_ID_Node(pDesc_arg, 0x02,
			USAGE_BAT_RUN_TIME_TO_EMPTY))) {
		upsdebugx(4, "Original Report Descriptor: ReportID 0x02 "
			"RunTimeToEmpty LogMin: %ld LogMax: %ld",
			pData->LogMin, pData->LogMax);

		if (pData->LogMax < pData->LogMin) {
			pData->LogMax = IDOWELL_RUNTIME_LOGMAX;
			pData->assumed_LogMax = true;
			upsdebugx(3, "Fixing Report Descriptor: set ReportID 0x02 "
				"RunTimeToEmpty LogMax = %ld",
				(long)IDOWELL_RUNTIME_LOGMAX);
			retval++;
		}
	}

	if ((pData = FindObject_with_ID_Node(pDesc_arg, 0x02,
			USAGE_POW_VOLTAGE))) {
		upsdebugx(4, "Original Report Descriptor: ReportID 0x02 "
			"Voltage LogMin: %ld LogMax: %ld",
			pData->LogMin, pData->LogMax);

		if (pData->LogMax < pData->LogMin) {
			pData->LogMax = IDOWELL_VOLTAGE_LOGMAX;
			pData->assumed_LogMax = true;
			upsdebugx(3, "Fixing Report Descriptor: set ReportID 0x02 "
				"Voltage LogMax = %ld",
				(long)IDOWELL_VOLTAGE_LOGMAX);
			retval++;
		}
	}

	/* This firmware pads RunTimeToEmpty to 32 bits in both the Feature
	 * 0x01 and the Input 0x02 copy, but only maintains 16 bits of it;
	 * the high byte of each copy carries adjacent PresentStatus state
	 * (e.g. 0x09 = ACPresent) which leaked into battery.runtime as
	 * 599940 = (0x09 << 16) + 10116. The mapping reads whichever copy
	 * the parser prefers, so shrink every oversized copy: the vendor
	 * only ever tracks 16-bit seconds (<= 65535 s ~ 18 h).
	 */
	for (int rid = 0; rid < 2; rid++) {
		const int report_id = (rid == 0) ? 0x01 : 0x02;

		if ((pData = FindObject_with_ID_Node(pDesc_arg, report_id,
		USAGE_BAT_RUN_TIME_TO_EMPTY)) == NULL) {
			upsdebugx(3, "Report Descriptor fix: no RunTimeToEmpty "
				"item found in ReportID 0x%02x", report_id);
			continue;
		}

		upsdebugx(4, "Original Report Descriptor: RunTimeToEmpty "
			"ReportID: 0x%02x, Offset: %d, Size: %d, "
			"LogMin: %ld, LogMax: %ld",
			report_id, (int)pData->Offset, (int)pData->Size,
			pData->LogMin, pData->LogMax);

		if (pData->Size <= 16) {
			continue;
		}

		upsdebugx(3, "Fixing Report Descriptor: set RunTimeToEmpty "
			"Size = 16 in ReportID 0x%02x (was %d: the high byte "
			"of this field carries adjacent firmware state such "
			"as the PresentStatus bitmap, which leaked into "
			"battery.runtime)",
			report_id, (int)pData->Size);
		pData->Size = 16;
		retval++;
	}

	retval += idowell_fix_status_usage(pDesc_arg,
		USAGE_BAT_AT_RATE_TIME_TO_EMPTY, USAGE_POW_GOOD);
	retval += idowell_fix_status_usage(pDesc_arg,
		USAGE_BAT_AVERAGE_TIME_TO_EMPTY, USAGE_POW_SHUTDOWN_IMMINENT);

	return retval;
}

/* (2) Live readings that the Report Descriptor does not declare.
 *
 * This firmware answers GET_REPORT for report IDs that the descriptor does
 * not declare (0x03..0x0f) with a 256 byte window into its RAM. One of those
 * windows holds a fixed-width ASCII digit status line - observed online with
 * the pack at 100 % on 2026-10-30, raw bytes:
 *
 *	99990900124010060022131000000000
 *	(999.9 090.0 124.0 100 60.0 2.21 31.0 00000000 after decoding)
 *
 * which decodes as: vendor runtime field (999.9 while online), a constant
 * 090.0 matching the low end of this model's spec input window (90-140 V,
 * line-interactive AVR buck threshold, not a measured bus), output voltage
 * (123.0 V; 119-120 while inverting; 000.0 when the outputs are dead),
 * battery charge (100 %), AC input frequency (59.9 Hz, 00.0 while
 * inverting), the pack voltage divided by a hardcoded 24 (per the vendor
 * spec the pack is LiFePO4 16S, nominal 51.2 V, 9 Ah, 460.8 Wh: the figure
 * scales back to 53.0 V at float and 40.1 V = 2.50 V/cell at the exact
 * point the BMS cuts), and a temperature-like field (31.0 idle, up to
 * 43.0 under load, matching the 0-40 C operating spec).
 *
 * The report ID acts as a window selector: the same page appears at ID 0x03
 * and 0x07 (line at offset 76/149) and at 0x0b and 0x0f (shifted by 5 bytes),
 * and is absent from the other IDs. So we scan a few IDs and anchor on the
 * ASCII pattern rather than on a fixed report ID or byte offset, and remember
 * which ID worked last.
 *
 * This block matters operationally: it is the only place where a dead output
 * is visible. When the pack cut out under load with the wall still unplugged,
 * the output field read 000.0 here while ups.status wrongly stayed OL.
 */
/* Report IDs that were seen to answer with the RAM window holding the line */
static const int	idowell_vendor_block_ids[] = { 0x03, 0x07, 0x0b, 0x0f };
static int		idowell_vendor_block_lastid = -1;
static int		idowell_vendor_block_fails = 0;

#define IDOWELL_VB_BUFLEN	256
#define IDOWELL_VB_LINELEN	96

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x)		((sizeof x) / (sizeof *x))
#endif
#define IDOWELL_VB_NONE			0
#define IDOWELL_VB_OK			(1 << 0)
#define IDOWELL_VB_OUTPUT_DEAD		(1 << 1)

static int		idowell_vendor_block_enabled = 0;
static int		idowell_vendor_block_amend_status = 0;
static int		idowell_vb_fell_back_output = 0;
static int		idowell_vb_fell_back_freq = 0;

/* Is this a device whose firmware needs the status block poll?
 * Cached, and switched off by the disable_idowell_vendor_block driver option. */
static int idowell_vendor_block_applies(HIDDevice_t *hd)
{
	static	int		known = -1;

	if (known < 0) {
		known = 0;
		if (hd != NULL
		&&  is_usb_device_supported(idowell_usb_device_table, hd) == SUPPORTED
		&&  !testvar("disable_idowell_vendor_block")
		) {
			/* The RAM-window status block, its tuple layout and the
			 * pack-voltage divisor of 24 are only proven on the
			 * 075d:0300 family (1000W/1200W/1600W models, all with
			 * vendor spec 51.2 V packs, all reporting the same 2.2
			 * scale figure per NUT issues #3556/#3015). Other IDs
			 * handled by this subdriver, e.g. 06da:ffff (25.6 V
			 * 8S packs per vendor spec), may share the firmware
			 * family but not this block's layout or divisor - see
			 * their descriptor Voltage figure of 13.2 in issue
			 * #3501 which on a 25.6 V pack implies a different
			 * scale. So default to 075d:0300 only; anything else
			 * needs explicit opt-in via idowell_vendor_block_force. */
			if (hd->VendorID == IDOWELL_VENDORID
			&&  hd->ProductID == 0x0300) {
				known = 1;
			} else if (testvar("idowell_vendor_block_force")) {
				known = 1;
				upsdebugx(2, "%s: vendor status block polling "
					"forced on an unverified device ID; "
					"decoded fields (including the x24 "
					"pack-voltage estimate) may be wrong",
					__func__);
			}
			if (known == 1)
				upsdebugx(2, "%s: will poll the vendor status "
					"block (disable with the "
					"disable_idowell_vendor_block option)",
					__func__);
		}
	}

	return known;
}

/* Vendor ups.conf options are offered through the aux hook set, so the
 * generic usbhid-ups code needs no iDowell specifics (pattern established
 * by ecoflow-hid-aux-cdc.c) */
static void idowell_vendor_block_makevartable(void)
{
	addvar(VAR_FLAG, "disable_idowell_vendor_block",
		"Do not poll the extra vendor status block that some iDowell "
		"and GoldenMate firmware only exposes through undeclared HID "
		"reports (raw GET_REPORT of a firmware RAM window)");

	addvar(VAR_FLAG, "idowell_vendor_block_status",
		"Let that vendor status block amend ups.status: when it "
		"measures the outputs dead (0.0 V) although the device claims "
		"to be online, report as on battery instead. This only ever "
		"downgrades a claimed online state; default off as it amends "
		"safety-relevant status based on an undeclared data source");

	addvar(VAR_FLAG, "idowell_vendor_block_force",
		"Poll and decode the vendor status block even on device IDs "
		"whose block layout and pack voltage scaling are not verified "
		"(decoded values may be nonsense there)");
}

static void idowell_vendor_block_initups(void)
{
	/* one-time decision, honoring the options above */
	idowell_vendor_block_amend_status =
		testvar("idowell_vendor_block_status") ? 1 : 0;
	idowell_vendor_block_enabled =
		idowell_vendor_block_applies(idowell_claimed_dev);
}

static int idowell_update_vendor_block(void)
{
	char		buf[IDOWELL_VB_BUFLEN];
	char		line[IDOWELL_VB_LINELEN];
	double		f1 = 0.0, f2 = 0.0, f3 = 0.0, f5 = 0.0, f6 = 0.0, f7 = 0.0;
	int		f4 = 0, nfields = 0, n = 0, id = -1, i, p, rc = IDOWELL_VB_NONE;

	/* try the report ID that worked last time, then sweep the known ones */
	for (i = -1; i < (int)ARRAY_SIZE(idowell_vendor_block_ids); i++) {
		int	try = (i < 0) ? idowell_vendor_block_lastid
		              : idowell_vendor_block_ids[i];

		if (try < 0 || try == id) {
			continue;
		}

		n = HIDGetRawReport(udev, try, buf, (int)sizeof(buf));
		if (n <= 0) {
			continue;
		}

		for (p = 0; p < n; p++) {
			int		run;

			if (buf[p] < '0' || buf[p] > '9') {
				continue;
			}

			/* measure the consecutive digit run */
			for (run = 0; (p + run) < n
			&&  buf[p + run] >= '0' && buf[p + run] <= '9'; run++) {}

			if (run < 32) {
				p += run;
				continue;
			}

			/* Raw format (verified on-wire, digits only, no separators;
			 * widths 4,4,4,3,3,3,3 then an 8 char flag word, with the
			 * decimal points implied by width):
			 *   9999 0900 1240 100 600 221 310 00000000
			 *    f1   f2   f3  f4  f5  f6  f7  flag
			 * Parse the fixed-width fields and rebuild a readable line. */
			{
				char	 fld[9];
				long	 v1, v2, v3, c4, v5, v6, v7;

				v1 = v2 = v3 = c4 = v5 = v6 = v7 = 0;

#define IDOWELL_VB_GET(dst, off, len) \
					do { \
						memcpy(fld, &buf[p + (off)], (len)); \
						fld[len] = '\0'; \
						dst = strtol(fld, NULL, 10); \
					} while (0)

				IDOWELL_VB_GET(v1, 0, 4);
				IDOWELL_VB_GET(v2, 4, 4);
				IDOWELL_VB_GET(v3, 8, 4);
				IDOWELL_VB_GET(c4, 12, 3);
				IDOWELL_VB_GET(v5, 15, 3);
				IDOWELL_VB_GET(v6, 18, 3);
				IDOWELL_VB_GET(v7, 21, 3);
#undef IDOWELL_VB_GET

				f1 = (double)v1 / 10.0;
				f2 = (double)v2 / 10.0;
				f3 = (double)v3 / 10.0;
				f4 = (int)c4;
				f5 = (double)v5 / 10.0;
				f6 = (double)v6 / 100.0;
				f7 = (double)v7 / 10.0;
				snprintf(line, sizeof(line),
					"%.1f %.1f %.1f %03d %.1f %.2f %.1f %.*s",
					f1, f2, f3, f4, f5, f6, f7,
					8, &buf[p + 24]);

				nfields = 7;
			}

			/* plausibility gate: charge is a percentage, output V is a
			 * line voltage, bus V is positive; anything else means we
			 * landed on unrelated RAM */
			if (f4 >= 0 && f4 <= 100
			&&  f2 > 0.0 && f2 < 1000.0
			&&  f3 >= 0.0 && f3 < 300.0
			) {
				id = try;
				break;
			}
			nfields = 0;
			p += 31;
		}

		if (nfields >= 6) {
			break;
		}
	}

	if (nfields < 6) {
		/* Do not keep publishing figures we can no longer confirm */
		if (++idowell_vendor_block_fails >= 3) {
			upsdebugx(2, "%s: vendor status block not readable for %d "
				"polls in a row, dropping the variables it fed",
				__func__, idowell_vendor_block_fails);
			if (idowell_vb_fell_back_output) {
				/* only delete the fallbacks we filled */
				dstate_delinfo("output.voltage");
				idowell_vb_fell_back_output = 0;
			}
			if (idowell_vb_fell_back_freq) {
				dstate_delinfo("input.frequency");
				idowell_vb_fell_back_freq = 0;
			}
			dstate_delinfo("experimental.idowell.battery.voltage.pack.estimated");
			dstate_delinfo("experimental.idowell.status_block.input_frequency");
			dstate_delinfo("experimental.idowell.status_block.output_voltage");
			dstate_delinfo("experimental.idowell.status_block.runtime_field");
			dstate_delinfo("experimental.idowell.status_block.temp_field");
			dstate_delinfo("experimental.idowell.status_block.tuple");
			dstate_delinfo("experimental.idowell.output.dead");
		}
		return rc;
	}

	idowell_vendor_block_fails = 0;
	idowell_vendor_block_lastid = id;

	upsdebugx(4, "%s: vendor status block (report 0x%02x): %s",
		__func__, id, line);

	/* The declared reports on this firmware DO provide output.voltage
	 * and input.frequency - but they keep serving bypass values while
	 * inverting (125.2 V / 60.0 Hz measured with the wall plug out and
	 * the outputs cut to 0.0), so they are not to be trusted in
	 * battery or dead-output states. We do not clobber declared-path
	 * variables; the honest measurements from this block are published
	 * under experimental.idowell.* names (docs/nut-names.txt rule for
	 * unstandardized concepts), so they are cross-checkable. */
	if (dstate_getinfo("output.voltage") == NULL) {
		/* fill the standard name only when this firmware does not
		 * serve it at all; declared copies must not be clobbered
		 * here, and are never deleted from this file */
		idowell_vb_fell_back_output = 1;
		dstate_setinfo("output.voltage", "%.1f", f3);
	}
	if (dstate_getinfo("input.frequency") == NULL) {
		idowell_vb_fell_back_freq = 1;
		dstate_setinfo("input.frequency", "%.1f", f5);
	}
	dstate_setinfo("experimental.idowell.status_block.output_voltage", "%.1f", f3);
	dstate_setinfo("experimental.idowell.status_block.input_frequency", "%.1f", f5);

	/* Vendor spec for this model (UPS2000VA/1600W): LiFePO4,
	 * nominal 51.2 V (16S), 9 Ah, 460.8 Wh. The per-cell-scale
	 * figure f6 tracks real pack state when scaled x24: 2.21 ->
	 * 53.0 V at float, and 1.67 -> 40.1 V = exactly the 2.50 V/cell
	 * where the BMS cuts at end-of-discharge. Publish that as an
	 * explicitly estimated pack voltage; the x24 divisor is
	 * empirical (no multimeter on a mains-connected pack), so the
	 * variable name says so. f2 is a constant 90.0 which matches
	 * the low end of the spec 90-140 V input window (AVR buck
	 * threshold), not any battery-side measurement. */
	dstate_setinfo("experimental.idowell.battery.voltage.pack.estimated", "%.1f", f6 * 24.0);
	dstate_setinfo("experimental.idowell.status_block.runtime_field", "%.1f", f1);
	if (nfields >= 7) {
		dstate_setinfo("experimental.idowell.status_block.temp_field", "%.1f", f7);
	}
	dstate_setinfo("experimental.idowell.status_block.tuple", "%s", line);

	/* (3) Dead outputs are the state that NUT otherwise gets wrong here:
	 * the PresentStatus bitmap keeps claiming ACPresent after the pack
	 * cut out with the wall unplugged. Flag it always; letting it amend
	 * ups.status is opt-in (see idowell_vendor_block_status). */
	dstate_setinfo("experimental.idowell.output.dead", "%d", (f3 < 1.0) ? 1 : 0);
	if (f3 < 1.0) {
		rc |= IDOWELL_VB_OUTPUT_DEAD;
	}

	return rc | IDOWELL_VB_OK;
}


/* Amend the just-committed ups.status where the measurement disagrees
 * with the PresentStatus bitmap: after the pack cut out with the wall
 * unplugged, these firmwares keep asserting ACPresent while this block
 * measures 0.0 V at the outputs. The aux updateinfo() hook runs right
 * after the generic driver committed ups.status for this cycle, so
 * rewrite that string to drop the OL claim and ensure OB. Only ever
 * downgrades the power state, and is opt-in (idowell_vendor_block_status).
 * Note the driver's own status-change logging then sees the amended
 * value as its baseline for the next cycle.
 */
static void idowell_amend_status_string(void)
{
	const char	*cur = dstate_getinfo("ups.status");
	char		buf[256];
	size_t		len = 0;
	int		online_seen = 0, battery_seen = 0;
	const char	*token = cur;

	if (cur == NULL) {
		return;
	}

	while (*token != '\0' && len < sizeof(buf) - 4) {
		const char	*end = token;

		while (*end != '\0' && *end != ' ') {
			end++;
		}

		if (end - token == 2 && !strncmp(token, "OL", 2)) {
			online_seen = 1;
		}
		else {
			if (end - token == 2 && !strncmp(token, "OB", 2)) {
				battery_seen = 1;
			}
			memcpy(&buf[len], token, (size_t)(end - token));
			len += (size_t)(end - token);
			buf[len++] = ' ';
		}

		token = (*end != '\0') ? end + 1 : end;
	}

	if (!online_seen) {
		/* the device already reports OB or bypass states: nothing
		 * to correct */
		return;
	}

	if (!battery_seen) {
		memmove(&buf[1], buf, len);
		buf[0] = 'O';
		buf[1] = 'B';
		len += 2;
	}
	buf[len] = '\0';

	upsdebugx(2, "%s: the vendor status block measured 0.0 V on the "
		"outputs although the device claims to be online: amending "
		"ups.status from '%s' to '%s'",
		__func__, cur, buf);

	dstate_setinfo("ups.status", "%s", buf);
}

static void idowell_vendor_block_updateinfo(void)
{
	int	vb;

	if (!idowell_vendor_block_enabled) {
		return;
	}

	vb = idowell_update_vendor_block();

	if (!(vb & IDOWELL_VB_OK)) {
		return;
	}

	/* Measured 0.0 V on the outputs: the firmware starts serving canned
	 * online-era figures (battery.runtime jumps to 10800..14400 seconds
	 * of remaining runtime with the outlets already dead). Remaining
	 * runtime behind dead outputs is zero, and a dead output must not
	 * leave ups.status claiming OL. Both amendments are gated on the
	 * opt-in idowell_vendor_block_status option.
	 */
	if (idowell_vendor_block_amend_status && (vb & IDOWELL_VB_OUTPUT_DEAD)) {
		dstate_setinfo("battery.runtime", "0");
		idowell_amend_status_string();
	}
}

/* The quirk plugs into the generic polling cycle through this hook set
 * (subdriver_aux_t, see usbhid-ups.h) */
subdriver_aux_t idowell_hid_aux_vendor_block = {
	idowell_vendor_block_makevartable,
	idowell_vendor_block_initups,
	NULL,	/* initinfo: nothing to add */
	idowell_vendor_block_updateinfo,
	NULL,	/* cleanup: nothing persistent to release */
};

subdriver_t idowell_subdriver = {
	IDOWELL_HID_VERSION,
	idowell_claim,
	idowell_utab,
	idowell_hid2nut,
	idowell_format_model,
	idowell_format_mfr,
	idowell_format_serial,
	idowell_fix_report_desc,
	&idowell_hid_aux_vendor_block,
};

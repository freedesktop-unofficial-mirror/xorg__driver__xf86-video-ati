/*
 * Copyright 2007  Egbert Eich   <eich@novell.com>
 * Copyright 2007  Luc Verhaegen <lverhaegen@novell.com>
 * Copyright 2007  Matthias Hopf <mhopf@novell.com>
 * Copyright 2007  Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "xf86.h"
#include "xf86_OSproc.h"

#include "radeon.h"
#include "radeon_atombios.h"
#include "radeon_atomwrapper.h"
#include "radeon_probe.h"
#include "radeon_macros.h"

#include "xorg-server.h"
#if XSERVER_LIBPCIACCESS
#warning pciaccess defined
#endif

/* only for testing now */
#include "xf86DDC.h"

typedef AtomBiosResult (*AtomBiosRequestFunc)(atomBiosHandlePtr handle,
					  AtomBiosRequestID unused, AtomBiosArgPtr data);
typedef struct rhdConnectorInfo *rhdConnectorInfoPtr;

static AtomBiosResult rhdAtomInit(atomBiosHandlePtr unused1,
				      AtomBiosRequestID unused2, AtomBiosArgPtr data);
static AtomBiosResult rhdAtomTearDown(atomBiosHandlePtr handle,
					  AtomBiosRequestID unused1, AtomBiosArgPtr unused2);
static AtomBiosResult rhdAtomVramInfoQuery(atomBiosHandlePtr handle,
					       AtomBiosRequestID func, AtomBiosArgPtr data);
static AtomBiosResult rhdAtomTmdsInfoQuery(atomBiosHandlePtr handle,
					       AtomBiosRequestID func, AtomBiosArgPtr data);
static AtomBiosResult rhdAtomAllocateFbScratch(atomBiosHandlePtr handle,
						   AtomBiosRequestID func, AtomBiosArgPtr data);
static AtomBiosResult rhdAtomLvdsGetTimings(atomBiosHandlePtr handle,
					AtomBiosRequestID unused, AtomBiosArgPtr data);
static AtomBiosResult rhdAtomLvdsInfoQuery(atomBiosHandlePtr handle,
					       AtomBiosRequestID func,  AtomBiosArgPtr data);
static AtomBiosResult rhdAtomGPIOI2CInfoQuery(atomBiosHandlePtr handle,
						  AtomBiosRequestID func, AtomBiosArgPtr data);
static AtomBiosResult rhdAtomFirmwareInfoQuery(atomBiosHandlePtr handle,
						   AtomBiosRequestID func, AtomBiosArgPtr data);
/*static AtomBiosResult rhdAtomConnectorInfo(atomBiosHandlePtr handle,
  AtomBiosRequestID unused, AtomBiosArgPtr data);*/
# ifdef ATOM_BIOS_PARSER
static AtomBiosResult rhdAtomExec(atomBiosHandlePtr handle,
				   AtomBiosRequestID unused, AtomBiosArgPtr data);
# endif
static AtomBiosResult
rhdAtomCompassionateDataQuery(atomBiosHandlePtr handle,
			      AtomBiosRequestID func, AtomBiosArgPtr data);


enum msgDataFormat {
    MSG_FORMAT_NONE,
    MSG_FORMAT_HEX,
    MSG_FORMAT_DEC
};

struct atomBIOSRequests {
    AtomBiosRequestID id;
    AtomBiosRequestFunc request;
    char *message;
    enum msgDataFormat message_format;
} AtomBiosRequestList [] = {
    {ATOMBIOS_INIT,			rhdAtomInit,
     "AtomBIOS Init",				MSG_FORMAT_NONE},
    {ATOMBIOS_TEARDOWN,			rhdAtomTearDown,
     "AtomBIOS Teardown",			MSG_FORMAT_NONE},
# ifdef ATOM_BIOS_PARSER
    {ATOMBIOS_EXEC,			rhdAtomExec,
     "AtomBIOS Exec",				MSG_FORMAT_NONE},
#endif
    {ATOMBIOS_ALLOCATE_FB_SCRATCH,	rhdAtomAllocateFbScratch,
     "AtomBIOS Set FB Space",			MSG_FORMAT_NONE},
    /*{ATOMBIOS_GET_CONNECTORS,		rhdAtomConnectorInfo,
      "AtomBIOS Get Connectors",			MSG_FORMAT_NONE},*/
    {ATOMBIOS_GET_PANEL_MODE,		rhdAtomLvdsGetTimings,
     "AtomBIOS Get Panel Mode",			MSG_FORMAT_NONE},
    {ATOMBIOS_GET_PANEL_EDID,		rhdAtomLvdsGetTimings,
     "AtomBIOS Get Panel EDID",			MSG_FORMAT_NONE},
    {GET_DEFAULT_ENGINE_CLOCK,		rhdAtomFirmwareInfoQuery,
     "Default Engine Clock",			MSG_FORMAT_DEC},
    {GET_DEFAULT_MEMORY_CLOCK,		rhdAtomFirmwareInfoQuery,
     "Default Memory Clock",			MSG_FORMAT_DEC},
    {GET_MAX_PIXEL_CLOCK_PLL_OUTPUT,	rhdAtomFirmwareInfoQuery,
     "Maximum Pixel ClockPLL Frequency Output", MSG_FORMAT_DEC},
    {GET_MIN_PIXEL_CLOCK_PLL_OUTPUT,	rhdAtomFirmwareInfoQuery,
     "Minimum Pixel ClockPLL Frequency Output", MSG_FORMAT_DEC},
    {GET_MAX_PIXEL_CLOCK_PLL_INPUT,	rhdAtomFirmwareInfoQuery,
     "Maximum Pixel ClockPLL Frequency Input", MSG_FORMAT_DEC},
    {GET_MIN_PIXEL_CLOCK_PLL_INPUT,	rhdAtomFirmwareInfoQuery,
     "Minimum Pixel ClockPLL Frequency Input", MSG_FORMAT_DEC},
    {GET_MAX_PIXEL_CLK,			rhdAtomFirmwareInfoQuery,
     "Maximum Pixel Clock",			MSG_FORMAT_DEC},
    {GET_REF_CLOCK,			rhdAtomFirmwareInfoQuery,
     "Reference Clock",				MSG_FORMAT_DEC},
    {GET_FW_FB_START,			rhdAtomVramInfoQuery,
      "Start of VRAM area used by Firmware",	MSG_FORMAT_HEX},
    {GET_FW_FB_SIZE,			rhdAtomVramInfoQuery,
      "Framebuffer space used by Firmware (kb)", MSG_FORMAT_DEC},
    {ATOM_TMDS_FREQUENCY,		rhdAtomTmdsInfoQuery,
     "TMDS Frequency",				MSG_FORMAT_DEC},
    {ATOM_TMDS_PLL_CHARGE_PUMP,		rhdAtomTmdsInfoQuery,
     "TMDS PLL ChargePump",			MSG_FORMAT_DEC},
    {ATOM_TMDS_PLL_DUTY_CYCLE,		rhdAtomTmdsInfoQuery,
     "TMDS PLL DutyCycle",			MSG_FORMAT_DEC},
    {ATOM_TMDS_PLL_VCO_GAIN,		rhdAtomTmdsInfoQuery,
     "TMDS PLL VCO Gain",			MSG_FORMAT_DEC},
    {ATOM_TMDS_PLL_VOLTAGE_SWING,	rhdAtomTmdsInfoQuery,
     "TMDS PLL VoltageSwing",			MSG_FORMAT_DEC},
    {ATOM_LVDS_SUPPORTED_REFRESH_RATE,	rhdAtomLvdsInfoQuery,
     "LVDS Supported Refresh Rate",		MSG_FORMAT_DEC},
    {ATOM_LVDS_OFF_DELAY,		rhdAtomLvdsInfoQuery,
     "LVDS Off Delay",				MSG_FORMAT_DEC},
    {ATOM_LVDS_SEQ_DIG_ONTO_DE,		rhdAtomLvdsInfoQuery,
     "LVDS SEQ Dig onto DE",			MSG_FORMAT_DEC},
    {ATOM_LVDS_SEQ_DE_TO_BL,		rhdAtomLvdsInfoQuery,
     "LVDS SEQ DE to BL",			MSG_FORMAT_DEC},
    {ATOM_LVDS_DITHER,			rhdAtomLvdsInfoQuery,
     "LVDS Ditherc",				MSG_FORMAT_HEX},
    {ATOM_LVDS_DUALLINK,		rhdAtomLvdsInfoQuery,
     "LVDS Duallink",				MSG_FORMAT_HEX},
    {ATOM_LVDS_GREYLVL,			rhdAtomLvdsInfoQuery,
     "LVDS Grey Level",				MSG_FORMAT_HEX},
    {ATOM_LVDS_FPDI,			rhdAtomLvdsInfoQuery,
     "LVDS FPDI",				MSG_FORMAT_HEX},
    {ATOM_LVDS_24BIT,			rhdAtomLvdsInfoQuery,
     "LVDS 24Bit",				MSG_FORMAT_HEX},
    {ATOM_GPIO_I2C_CLK_MASK,		rhdAtomGPIOI2CInfoQuery,
     "GPIO_I2C_Clk_Mask",			MSG_FORMAT_HEX},
    {ATOM_DAC1_BG_ADJ,		rhdAtomCompassionateDataQuery,
     "DAC1 BG Adjustment",			MSG_FORMAT_HEX},
    {ATOM_DAC1_DAC_ADJ,		rhdAtomCompassionateDataQuery,
     "DAC1 DAC Adjustment",			MSG_FORMAT_HEX},
    {ATOM_DAC1_FORCE,		rhdAtomCompassionateDataQuery,
     "DAC1 Force Data",				MSG_FORMAT_HEX},
    {ATOM_DAC2_CRTC2_BG_ADJ,	rhdAtomCompassionateDataQuery,
     "DAC2_CRTC2 BG Adjustment",		MSG_FORMAT_HEX},
    {ATOM_DAC2_CRTC2_DAC_ADJ,	rhdAtomCompassionateDataQuery,
     "DAC2_CRTC2 DAC Adjustment",		MSG_FORMAT_HEX},
    {ATOM_DAC2_CRTC2_FORCE,	rhdAtomCompassionateDataQuery,
     "DAC2_CRTC2 Force",			MSG_FORMAT_HEX},
    {ATOM_DAC2_CRTC2_MUX_REG_IND,rhdAtomCompassionateDataQuery,
     "DAC2_CRTC2 Mux Register Index",		MSG_FORMAT_HEX},
    {ATOM_DAC2_CRTC2_MUX_REG_INFO,rhdAtomCompassionateDataQuery,
     "DAC2_CRTC2 Mux Register Info",		MSG_FORMAT_HEX},
    {FUNC_END,					NULL,
     NULL,					MSG_FORMAT_NONE}
};

enum {
    legacyBIOSLocation = 0xC0000,
    legacyBIOSMax = 0x10000
};

#define DEBUGP(x) {x;}
#define LOG_DEBUG 7

#  ifdef ATOM_BIOS_PARSER

#   define LOG_CAIL LOG_DEBUG + 1

static void
RHDDebug(int scrnIndex, const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    xf86VDrvMsgVerb(scrnIndex, X_INFO, LOG_DEBUG, format, ap);
    va_end(ap);
}

static void
RHDDebugCont(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    xf86VDrvMsgVerb(-1, X_NONE, LOG_DEBUG, format, ap);
    va_end(ap);
}

static void
CailDebug(int scrnIndex, const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    xf86VDrvMsgVerb(scrnIndex, X_INFO, LOG_CAIL, format, ap);
    va_end(ap);
}
#   define CAILFUNC(ptr) \
  CailDebug(((atomBiosHandlePtr)(ptr))->scrnIndex, "CAIL: %s\n", __func__)

#  endif

static int
rhdAtomAnalyzeCommonHdr(ATOM_COMMON_TABLE_HEADER *hdr)
{
    if (hdr->usStructureSize == 0xaa55)
        return FALSE;

    return TRUE;
}

static int
rhdAtomAnalyzeRomHdr(unsigned char *rombase,
		     ATOM_ROM_HEADER *hdr,
		     unsigned int *data_offset, 
		     unsigned int *command_offset)
{
    if (!rhdAtomAnalyzeCommonHdr(&hdr->sHeader)) {
        return FALSE;
    }
    xf86DrvMsg(-1,X_NONE,"\tSubsystemVendorID: 0x%4.4x SubsystemID: 0x%4.4x\n",
               hdr->usSubsystemVendorID,hdr->usSubsystemID);
    xf86DrvMsg(-1,X_NONE,"\tIOBaseAddress: 0x%4.4x\n",hdr->usIoBaseAddress);
    xf86DrvMsgVerb(-1,X_NONE,3,"\tFilename: %s\n",rombase + hdr->usConfigFilenameOffset);
    xf86DrvMsgVerb(-1,X_NONE,3,"\tBIOS Bootup Message: %s\n",
		   rombase + hdr->usBIOS_BootupMessageOffset);

    *data_offset = hdr->usMasterDataTableOffset;
    *command_offset = hdr->usMasterCommandTableOffset;

    return TRUE;
}

static int
rhdAtomAnalyzeRomDataTable(unsigned char *base, int offset,
                    void *ptr,unsigned short *size)
{
    ATOM_COMMON_TABLE_HEADER *table = (ATOM_COMMON_TABLE_HEADER *)
        (base + offset);

   if (!*size || !rhdAtomAnalyzeCommonHdr(table)) {
       if (*size) *size -= 2;
       *(void **)ptr = NULL;
       return FALSE;
   }
   *size -= 2;
   *(void **)ptr = (void *)(table);
   return TRUE;
}

Bool
rhdAtomGetTableRevisionAndSize(ATOM_COMMON_TABLE_HEADER *hdr,
			       CARD8 *contentRev,
			       CARD8 *formatRev,
			       unsigned short *size)
{
    if (!hdr)
        return FALSE;

    if (contentRev) *contentRev = hdr->ucTableContentRevision;
    if (formatRev) *formatRev = hdr->ucTableFormatRevision;
    if (size) *size = (short)hdr->usStructureSize
                   - sizeof(ATOM_COMMON_TABLE_HEADER);
    return TRUE;
}

static Bool
rhdAtomAnalyzeMasterDataTable(unsigned char *base,
			      ATOM_MASTER_DATA_TABLE *table,
			      atomDataTablesPtr data)
{
    ATOM_MASTER_LIST_OF_DATA_TABLES *data_table =
        &table->ListOfDataTables;
    unsigned short size;

    if (!rhdAtomAnalyzeCommonHdr(&table->sHeader))
        return FALSE;
    if (!rhdAtomGetTableRevisionAndSize(&table->sHeader,NULL,NULL,
					&size))
        return FALSE;
# define SET_DATA_TABLE(x) {\
   rhdAtomAnalyzeRomDataTable(base,data_table->x,(void *)(&(data->x)),&size); \
    }

# define SET_DATA_TABLE_VERS(x) {\
   rhdAtomAnalyzeRomDataTable(base,data_table->x,&(data->x.base),&size); \
    }

    SET_DATA_TABLE(UtilityPipeLine);
    SET_DATA_TABLE(MultimediaCapabilityInfo);
    SET_DATA_TABLE(MultimediaConfigInfo);
    SET_DATA_TABLE(StandardVESA_Timing);
    SET_DATA_TABLE_VERS(FirmwareInfo);
    SET_DATA_TABLE(DAC_Info);
    SET_DATA_TABLE_VERS(LVDS_Info);
    SET_DATA_TABLE(TMDS_Info);
    SET_DATA_TABLE(AnalogTV_Info);
    SET_DATA_TABLE_VERS(SupportedDevicesInfo);
    SET_DATA_TABLE(GPIO_I2C_Info);
    SET_DATA_TABLE(VRAM_UsageByFirmware);
    SET_DATA_TABLE(GPIO_Pin_LUT);
    SET_DATA_TABLE(VESA_ToInternalModeLUT);
    SET_DATA_TABLE_VERS(ComponentVideoInfo);
    SET_DATA_TABLE(PowerPlayInfo);
    SET_DATA_TABLE(CompassionateData);
    SET_DATA_TABLE(SaveRestoreInfo);
    SET_DATA_TABLE(PPLL_SS_Info);
    SET_DATA_TABLE(OemInfo);
    SET_DATA_TABLE(XTMDS_Info);
    SET_DATA_TABLE(MclkSS_Info);
    SET_DATA_TABLE(Object_Header);
    SET_DATA_TABLE(IndirectIOAccess);
    SET_DATA_TABLE(MC_InitParameter);
    SET_DATA_TABLE(ASIC_VDDC_Info);
    SET_DATA_TABLE(ASIC_InternalSS_Info);
    SET_DATA_TABLE(TV_VideoMode);
    SET_DATA_TABLE_VERS(VRAM_Info);
    SET_DATA_TABLE(MemoryTrainingInfo);
    SET_DATA_TABLE_VERS(IntegratedSystemInfo);
    SET_DATA_TABLE(ASIC_ProfilingInfo);
    SET_DATA_TABLE(VoltageObjectInfo);
    SET_DATA_TABLE(PowerSourceInfo);
# undef SET_DATA_TABLE

    return TRUE;
}

static Bool
rhdAtomGetDataTable(int scrnIndex,
		    unsigned char *base,
		    atomDataTables *atomDataPtr,
		    unsigned int *cmd_offset,
		    unsigned int BIOSImageSize)
{
    unsigned int data_offset;
    unsigned int atom_romhdr_off =  *(unsigned short*)
        (base + OFFSET_TO_POINTER_TO_ATOM_ROM_HEADER);
    ATOM_ROM_HEADER *atom_rom_hdr =
        (ATOM_ROM_HEADER *)(base + atom_romhdr_off);

    //RHDFUNCI(scrnIndex);

    if (atom_romhdr_off + sizeof(ATOM_ROM_HEADER) > BIOSImageSize) {
	xf86DrvMsg(scrnIndex,X_ERROR,
		   "%s: AtomROM header extends beyond BIOS image\n",__func__);
	return FALSE;
    }

    if (memcmp("ATOM",&atom_rom_hdr->uaFirmWareSignature,4)) {
        xf86DrvMsg(scrnIndex,X_ERROR,"%s: No AtomBios signature found\n",
		   __func__);
        return FALSE;
    }
    xf86DrvMsg(scrnIndex, X_INFO, "ATOM BIOS Rom: \n");
    if (!rhdAtomAnalyzeRomHdr(base, atom_rom_hdr, &data_offset, cmd_offset)) {
        xf86DrvMsg(scrnIndex, X_ERROR, "RomHeader invalid\n");
        return FALSE;
    }

    if (data_offset + sizeof (ATOM_MASTER_DATA_TABLE) > BIOSImageSize) {
	xf86DrvMsg(scrnIndex,X_ERROR,"%s: Atom data table outside of BIOS\n",
		   __func__);
    }

    if (*cmd_offset + sizeof (ATOM_MASTER_COMMAND_TABLE) > BIOSImageSize) {
	xf86DrvMsg(scrnIndex,X_ERROR,"%s: Atom command table outside of BIOS\n",
		   __func__);
    }

    if (!rhdAtomAnalyzeMasterDataTable(base, (ATOM_MASTER_DATA_TABLE *)
				       (base + data_offset),
				       atomDataPtr)) {
        xf86DrvMsg(scrnIndex, X_ERROR, "%s: ROM Master Table invalid\n",
		   __func__);
        return FALSE;
    }
    return TRUE;
}

static Bool
rhdAtomGetFbBaseAndSize(atomBiosHandlePtr handle, unsigned int *base,
			unsigned int *size)
{
    AtomBiosArgRec data;
    if (RHDAtomBiosFunc(handle->scrnIndex, handle, GET_FW_FB_SIZE, &data)
	== ATOM_SUCCESS) {
	if (data.val == 0) {
	    xf86DrvMsg(handle->scrnIndex, X_WARNING, "%s: AtomBIOS specified VRAM "
		       "scratch space size invalid\n", __func__);
	    return FALSE;
	}
	if (size)
	    *size = (int)data.val;
    } else
	return FALSE;
    if (RHDAtomBiosFunc(handle->scrnIndex, handle, GET_FW_FB_START, &data)
	== ATOM_SUCCESS) {
	if (data.val == 0)
	    return FALSE;
	if (base)
	    *base = (int)data.val;
    }
    return TRUE;
}

/*
 * Uses videoRam form ScrnInfoRec.
 */
static AtomBiosResult
rhdAtomAllocateFbScratch(atomBiosHandlePtr handle,
			 AtomBiosRequestID func, AtomBiosArgPtr data)
{
    unsigned int fb_base = 0;
    unsigned int fb_size = 0;
    unsigned int start = data->fb.start;
    unsigned int size = data->fb.size;
    handle->scratchBase = NULL;
    handle->fbBase = 0;

    if (rhdAtomGetFbBaseAndSize(handle, &fb_base, &fb_size)) {
	xf86DrvMsg(handle->scrnIndex, X_INFO, "AtomBIOS requests %ikB"
		   " of VRAM scratch space\n",fb_size);
	fb_size *= 1024; /* convert to bytes */
	xf86DrvMsg(handle->scrnIndex, X_INFO, "AtomBIOS VRAM scratch base: 0x%x\n",
		   fb_base);
    } else {
	    fb_size = 20 * 1024;
	    xf86DrvMsg(handle->scrnIndex, X_INFO, " default to: %i\n",fb_size);
    }
    if (fb_base && fb_size && size) {
	/* 4k align */
	fb_size = (fb_size & ~(CARD32)0xfff) + ((fb_size & 0xfff) ? 1 : 0);
	if ((fb_base + fb_size) > (start + size)) {
	    xf86DrvMsg(handle->scrnIndex, X_WARNING,
		       "%s: FW FB scratch area %i (size: %i)"
		       " extends beyond available framebuffer size %i\n",
		       __func__, fb_base, fb_size, size);
	} else if ((fb_base + fb_size) < (start + size)) {
	    xf86DrvMsg(handle->scrnIndex, X_WARNING,
		       "%s: FW FB scratch area not located "
		       "at the end of VRAM. Scratch End: "
		       "0x%x VRAM End: 0x%x\n", __func__,
		       (unsigned int)(fb_base + fb_size),
		       size);
	} else if (fb_base < start) {
	    xf86DrvMsg(handle->scrnIndex, X_WARNING,
		       "%s: FW FB scratch area extends below "
		       "the base of the free VRAM: 0x%x Base: 0x%x\n",
		       __func__, (unsigned int)(fb_base), start);
	} else {
	    size -= fb_size;
	    handle->fbBase = fb_base;
	    return ATOM_SUCCESS;
	}
    }

    if (!handle->fbBase) {
	xf86DrvMsg(handle->scrnIndex, X_INFO,
		   "Cannot get VRAM scratch space. "
		   "Allocating in main memory instead\n");
	handle->scratchBase = xcalloc(fb_size,1);
	return ATOM_SUCCESS;
    }
    return ATOM_FAILED;
}

# ifdef ATOM_BIOS_PARSER
static Bool
rhdAtomASICInit(atomBiosHandlePtr handle)
{
    ASIC_INIT_PS_ALLOCATION asicInit;
    AtomBiosArgRec data;

    RHDAtomBiosFunc(handle->scrnIndex, handle,
		    GET_DEFAULT_ENGINE_CLOCK,
		    &data);
    asicInit.sASICInitClocks.ulDefaultEngineClock = data.val / 10;/*in 10 Khz*/
    RHDAtomBiosFunc(handle->scrnIndex, handle,
		    GET_DEFAULT_MEMORY_CLOCK,
		    &data);
    asicInit.sASICInitClocks.ulDefaultMemoryClock = data.val / 10;/*in 10 Khz*/
    data.exec.dataSpace = NULL;
    data.exec.index = 0x0;
    data.exec.pspace = &asicInit;
    xf86DrvMsg(handle->scrnIndex, X_INFO, "Calling ASIC Init\n");
    if (RHDAtomBiosFunc(handle->scrnIndex, handle,
			ATOMBIOS_EXEC, &data) == ATOM_SUCCESS) {
	xf86DrvMsg(handle->scrnIndex, X_INFO, "ASIC_INIT Successful\n");
	return TRUE;
    }
    xf86DrvMsg(handle->scrnIndex, X_INFO, "ASIC_INIT Failed\n");
    return FALSE;
}

Bool
rhdAtomSetScaler(atomBiosHandlePtr handle, unsigned char scalerID, int setting)
{
    ENABLE_SCALER_PARAMETERS scaler;
    AtomBiosArgRec data;

    scaler.ucScaler = scalerID;
    scaler.ucEnable = setting;
    data.exec.dataSpace = NULL;
    data.exec.index = 0x21;
    data.exec.pspace = &scaler;
    xf86DrvMsg(handle->scrnIndex, X_INFO, "Calling EnableScaler\n");
    if (RHDAtomBiosFunc(handle->scrnIndex, handle,
			ATOMBIOS_EXEC, &data) == ATOM_SUCCESS) {
	xf86DrvMsg(handle->scrnIndex, X_INFO, "EnableScaler Successful\n");
	return TRUE;
    }
    xf86DrvMsg(handle->scrnIndex, X_INFO, "EableScaler Failed\n");
    return FALSE;
}

# endif

static AtomBiosResult
rhdAtomInit(atomBiosHandlePtr unused1, AtomBiosRequestID unused2,
		    AtomBiosArgPtr data)
{
    int scrnIndex = data->val;
    RADEONInfoPtr  info   = RADEONPTR(xf86Screens[scrnIndex]);
    unsigned char *ptr;
    atomDataTablesPtr atomDataPtr;
    unsigned int cmd_offset;
    atomBiosHandlePtr handle = NULL;
    unsigned int BIOSImageSize = 0;
    data->atomhandle = NULL;

    //RHDFUNCI(scrnIndex);

    /*if (info->BIOSCopy) {
	xf86DrvMsg(scrnIndex,X_INFO,"Getting BIOS copy from INT10\n");
	ptr = info->BIOSCopy;
	info->BIOSCopy = NULL;

	BIOSImageSize = ptr[2] * 512;
	if (BIOSImageSize > legacyBIOSMax) {
	    xf86DrvMsg(scrnIndex,X_ERROR,"Invalid BIOS length field\n");
	    return ATOM_FAILED;
	}
    } else*/ {
	/*if (!xf86IsEntityPrimary(info->entityIndex)) {
	    if (!(BIOSImageSize = RHDReadPCIBios(info, &ptr)))
		return ATOM_FAILED;
	} else*/ {
	     int read_len;
	    unsigned char tmp[32];
	    xf86DrvMsg(scrnIndex,X_INFO,"Getting BIOS copy from legacy VBIOS location\n");
	    if (xf86ReadBIOS(legacyBIOSLocation, 0, tmp, 32) < 0) {
		xf86DrvMsg(scrnIndex,X_ERROR,
			   "Cannot obtain POSTed BIOS header\n");
		return ATOM_FAILED;
	    }
	    BIOSImageSize = tmp[2] * 512;
	    if (BIOSImageSize > legacyBIOSMax) {
		xf86DrvMsg(scrnIndex,X_ERROR,"Invalid BIOS length field\n");
		return ATOM_FAILED;
	    }
	    if (!(ptr = xcalloc(1,BIOSImageSize))) {
		xf86DrvMsg(scrnIndex,X_ERROR,
			   "Cannot allocate %i bytes of memory "
			   "for BIOS image\n",BIOSImageSize);
		return ATOM_FAILED;
	    }
	    if ((read_len = xf86ReadBIOS(legacyBIOSLocation, 0, ptr, BIOSImageSize)
		 < 0)) {
		xf86DrvMsg(scrnIndex,X_ERROR,"Cannot read POSTed BIOS\n");
		goto error;
	    }
	}
    }

    if (!(atomDataPtr = xcalloc(1, sizeof(atomDataTables)))) {
	xf86DrvMsg(scrnIndex,X_ERROR,"Cannot allocate memory for "
		   "ATOM BIOS data tabes\n");
	goto error;
    }
    if (!rhdAtomGetDataTable(scrnIndex, ptr, atomDataPtr, &cmd_offset, BIOSImageSize))
	goto error1;
    if (!(handle = xcalloc(1, sizeof(atomBiosHandleRec)))) {
	xf86DrvMsg(scrnIndex,X_ERROR,"Cannot allocate memory\n");
	goto error1;
    }
    handle->BIOSBase = ptr;
    handle->atomDataPtr = atomDataPtr;
    handle->cmd_offset = cmd_offset;
    handle->scrnIndex = scrnIndex;
#if XSERVER_LIBPCIACCESS
    handle->device = info->PciInfo;
#else
    handle->PciTag = info->PciTag;
#endif
    handle->BIOSImageSize = BIOSImageSize;

# if ATOM_BIOS_PARSER
    /* Try to find out if BIOS has been posted (either by system or int10 */
    if (!rhdAtomGetFbBaseAndSize(handle, NULL, NULL)) {
	/* run AsicInit */
	if (!rhdAtomASICInit(handle))
	    xf86DrvMsg(scrnIndex, X_WARNING,
		       "%s: AsicInit failed. Won't be able to obtain in VRAM "
		       "FB scratch space\n",__func__);
    }
# endif

    data->atomhandle = handle;
    return ATOM_SUCCESS;

 error1:
    xfree(atomDataPtr);
 error:
    xfree(ptr);
    return ATOM_FAILED;
}

static AtomBiosResult
rhdAtomTearDown(atomBiosHandlePtr handle,
		AtomBiosRequestID unused1, AtomBiosArgPtr unused2)
{
    //RHDFUNC(handle);

    xfree(handle->BIOSBase);
    xfree(handle->atomDataPtr);
    if (handle->scratchBase) xfree(handle->scratchBase);
    xfree(handle);
    return ATOM_SUCCESS;
}

static AtomBiosResult
rhdAtomVramInfoQuery(atomBiosHandlePtr handle, AtomBiosRequestID func,
		     AtomBiosArgPtr data)
{
    atomDataTablesPtr atomDataPtr;
    CARD32 *val = &data->val;
    //RHDFUNC(handle);

    atomDataPtr = handle->atomDataPtr;

    switch (func) {
	case GET_FW_FB_START:
	    *val = atomDataPtr->VRAM_UsageByFirmware
		->asFirmwareVramReserveInfo[0].ulStartAddrUsedByFirmware;
	    break;
	case GET_FW_FB_SIZE:
	    *val = atomDataPtr->VRAM_UsageByFirmware
		->asFirmwareVramReserveInfo[0].usFirmwareUseInKb;
	    break;
	default:
	    return ATOM_NOT_IMPLEMENTED;
    }
    return ATOM_SUCCESS;
}

static AtomBiosResult
rhdAtomTmdsInfoQuery(atomBiosHandlePtr handle,
		     AtomBiosRequestID func, AtomBiosArgPtr data)
{
    atomDataTablesPtr atomDataPtr;
    CARD32 *val = &data->val;
    int idx = *val;

    atomDataPtr = handle->atomDataPtr;
    if (!rhdAtomGetTableRevisionAndSize(
	    (ATOM_COMMON_TABLE_HEADER *)(atomDataPtr->TMDS_Info),
	    NULL,NULL,NULL)) {
	return ATOM_FAILED;
    }

    //RHDFUNC(handle);

    switch (func) {
	case ATOM_TMDS_FREQUENCY:
	    *val = atomDataPtr->TMDS_Info->asMiscInfo[idx].usFrequency;
	    break;
	case ATOM_TMDS_PLL_CHARGE_PUMP:
	    *val = atomDataPtr->TMDS_Info->asMiscInfo[idx].ucPLL_ChargePump;
	    break;
	case ATOM_TMDS_PLL_DUTY_CYCLE:
	    *val = atomDataPtr->TMDS_Info->asMiscInfo[idx].ucPLL_DutyCycle;
	    break;
	case ATOM_TMDS_PLL_VCO_GAIN:
	    *val = atomDataPtr->TMDS_Info->asMiscInfo[idx].ucPLL_VCO_Gain;
	    break;
	case ATOM_TMDS_PLL_VOLTAGE_SWING:
	    *val = atomDataPtr->TMDS_Info->asMiscInfo[idx].ucPLL_VoltageSwing;
	    break;
	default:
	    return ATOM_NOT_IMPLEMENTED;
    }
    return ATOM_SUCCESS;
}

static DisplayModePtr
rhdAtomLvdsTimings(atomBiosHandlePtr handle, ATOM_DTD_FORMAT *dtd)
{
    DisplayModePtr mode;
#define NAME_LEN 16
    char name[NAME_LEN];

    //RHDFUNC(handle);

    if (!(mode = (DisplayModePtr)xcalloc(1,sizeof(DisplayModeRec))))
	return NULL;

    mode->CrtcHDisplay = mode->HDisplay = dtd->usHActive;
    mode->CrtcVDisplay = mode->VDisplay = dtd->usVActive;
    mode->CrtcHBlankStart = dtd->usHActive + dtd->ucHBorder;
    mode->CrtcHBlankEnd = mode->CrtcHBlankStart + dtd->usHBlanking_Time;
    mode->CrtcHTotal = mode->HTotal = mode->CrtcHBlankEnd + dtd->ucHBorder;
    mode->CrtcVBlankStart = dtd->usVActive + dtd->ucVBorder;
    mode->CrtcVBlankEnd = mode->CrtcVBlankStart + dtd->usVBlanking_Time;
    mode->CrtcVTotal = mode->VTotal = mode->CrtcVBlankEnd + dtd->ucVBorder;
    mode->CrtcHSyncStart = mode->HSyncStart = dtd->usHActive + dtd->usHSyncOffset;
    mode->CrtcHSyncEnd = mode->HSyncEnd = mode->HSyncStart + dtd->usHSyncWidth;
    mode->CrtcVSyncStart = mode->VSyncStart = dtd->usVActive + dtd->usVSyncOffset;
    mode->CrtcVSyncEnd = mode->VSyncEnd = mode->VSyncStart + dtd->usVSyncWidth;

    mode->SynthClock = mode->Clock  = dtd->usPixClk * 10;

    mode->HSync = ((float) mode->Clock) / ((float)mode->HTotal);
    mode->VRefresh = (1000.0 * ((float) mode->Clock))
	/ ((float)(((float)mode->HTotal) * ((float)mode->VTotal)));

    snprintf(name, NAME_LEN, "%dx%d",
	     mode->HDisplay, mode->VDisplay);
    mode->name = xstrdup(name);

    RHDDebug(handle->scrnIndex,"%s: LVDS Modeline: %s  "
	     "%2.d  %i (%i) %i %i (%i) %i  %i (%i) %i %i (%i) %i\n",
	     __func__, mode->name, mode->Clock,
	     mode->HDisplay, mode->CrtcHBlankStart, mode->HSyncStart, mode->CrtcHSyncEnd,
	     mode->CrtcHBlankEnd, mode->HTotal,
	     mode->VDisplay, mode->CrtcVBlankStart, mode->VSyncStart, mode->VSyncEnd,
	     mode->CrtcVBlankEnd, mode->VTotal);

    return mode;
}

static unsigned char*
rhdAtomLvdsDDC(atomBiosHandlePtr handle, CARD32 offset, unsigned char *record)
{
    unsigned char *EDIDBlock;

    //RHDFUNC(handle);

    while (*record != ATOM_RECORD_END_TYPE) {

	switch (*record) {
	    case LCD_MODE_PATCH_RECORD_MODE_TYPE:
		offset += sizeof(ATOM_PATCH_RECORD_MODE);
		if (offset > handle->BIOSImageSize) break;
		record += sizeof(ATOM_PATCH_RECORD_MODE);
		break;

	    case LCD_RTS_RECORD_TYPE:
		offset += sizeof(ATOM_LCD_RTS_RECORD);
		if (offset > handle->BIOSImageSize) break;
		record += sizeof(ATOM_LCD_RTS_RECORD);
		break;

	    case LCD_CAP_RECORD_TYPE:
		offset += sizeof(ATOM_LCD_MODE_CONTROL_CAP);
		if (offset > handle->BIOSImageSize) break;
		record += sizeof(ATOM_LCD_MODE_CONTROL_CAP);
		break;

	    case LCD_FAKE_EDID_PATCH_RECORD_TYPE:
		offset += sizeof(ATOM_FAKE_EDID_PATCH_RECORD);
		/* check if the structure still fully lives in the BIOS image */
		if (offset > handle->BIOSImageSize) break;
		offset += ((ATOM_FAKE_EDID_PATCH_RECORD*)record)->ucFakeEDIDLength
		    - sizeof(UCHAR);
		if (offset > handle->BIOSImageSize) break;
		/* dup string as we free it later */
		if (!(EDIDBlock = (unsigned char *)xalloc(
			  ((ATOM_FAKE_EDID_PATCH_RECORD*)record)->ucFakeEDIDLength)))
		    return NULL;
		memcpy(EDIDBlock,&((ATOM_FAKE_EDID_PATCH_RECORD*)record)->ucFakeEDIDString,
		       ((ATOM_FAKE_EDID_PATCH_RECORD*)record)->ucFakeEDIDLength);

		/* for testing */
		{
		    xf86MonPtr mon = xf86InterpretEDID(handle->scrnIndex,EDIDBlock);
		    xf86PrintEDID(mon);
		    xfree(mon);
		}
		return EDIDBlock;

	    case LCD_PANEL_RESOLUTION_RECORD_TYPE:
		offset += sizeof(ATOM_PANEL_RESOLUTION_PATCH_RECORD);
		if (offset > handle->BIOSImageSize) break;
		record += sizeof(ATOM_PANEL_RESOLUTION_PATCH_RECORD);
		break;

	    default:
		xf86DrvMsg(handle->scrnIndex, X_ERROR,
			   "%s: unknown record type: %x\n",__func__,*record);
		return NULL;
	}
    }

    return NULL;
}

static AtomBiosResult
rhdAtomLvdsGetTimings(atomBiosHandlePtr handle, AtomBiosRequestID func,
		  AtomBiosArgPtr data)
{
    atomDataTablesPtr atomDataPtr;
    CARD8 crev, frev;
    unsigned long offset;

    //RHDFUNC(handle);

    atomDataPtr = handle->atomDataPtr;

    if (!rhdAtomGetTableRevisionAndSize(
	    (ATOM_COMMON_TABLE_HEADER *)(atomDataPtr->LVDS_Info.base),
	    &frev,&crev,NULL)) {
	return ATOM_FAILED;
    }

    switch (crev) {

	case 1:
	    switch (func) {
		case ATOMBIOS_GET_PANEL_MODE:
		    data->mode = rhdAtomLvdsTimings(handle,
						    &atomDataPtr->LVDS_Info
						    .LVDS_Info->sLCDTiming);
		    if (data->mode)
			return ATOM_SUCCESS;
		default:
		    return ATOM_FAILED;
	    }
	case 2:
	    switch (func) {
		case ATOMBIOS_GET_PANEL_MODE:
		    data->mode = rhdAtomLvdsTimings(handle,
						    &atomDataPtr->LVDS_Info
						    .LVDS_Info_v12->sLCDTiming);
		    if (data->mode)
			return ATOM_SUCCESS;
		    return ATOM_FAILED;

		case ATOMBIOS_GET_PANEL_EDID:
		    offset = (unsigned long)&atomDataPtr->LVDS_Info.base
			- (unsigned long)handle->BIOSBase
			+ atomDataPtr->LVDS_Info
			.LVDS_Info_v12->usExtInfoTableOffset;

		    data->EDIDBlock
			= rhdAtomLvdsDDC(handle, offset,
					 (unsigned char *)
					 &atomDataPtr->LVDS_Info.base
					 + atomDataPtr->LVDS_Info
					 .LVDS_Info_v12->usExtInfoTableOffset);
		    if (data->EDIDBlock)
			return ATOM_SUCCESS;
		default:
		    return ATOM_FAILED;
	    }
	default:
	    return ATOM_NOT_IMPLEMENTED;
    }
/*NOTREACHED*/
}

static AtomBiosResult
rhdAtomLvdsInfoQuery(atomBiosHandlePtr handle,
		     AtomBiosRequestID func,  AtomBiosArgPtr data)
{
    atomDataTablesPtr atomDataPtr;
    CARD8 crev, frev;
    CARD32 *val = &data->val;

    //RHDFUNC(handle);

    atomDataPtr = handle->atomDataPtr;

    if (!rhdAtomGetTableRevisionAndSize(
	    (ATOM_COMMON_TABLE_HEADER *)(atomDataPtr->LVDS_Info.base),
	    &frev,&crev,NULL)) {
	return ATOM_FAILED;
    }

    switch (crev) {
	case 1:
	    switch (func) {
		case ATOM_LVDS_SUPPORTED_REFRESH_RATE:
		    *val = atomDataPtr->LVDS_Info
			.LVDS_Info->usSupportedRefreshRate;
		    break;
		case ATOM_LVDS_OFF_DELAY:
		    *val = atomDataPtr->LVDS_Info
			.LVDS_Info->usOffDelayInMs;
		    break;
		case ATOM_LVDS_SEQ_DIG_ONTO_DE:
		    *val = atomDataPtr->LVDS_Info
			.LVDS_Info->ucPowerSequenceDigOntoDEin10Ms * 10;
		    break;
		case ATOM_LVDS_SEQ_DE_TO_BL:
		    *val = atomDataPtr->LVDS_Info
			.LVDS_Info->ucPowerSequenceDEtoBLOnin10Ms * 10;
		    break;
		case     ATOM_LVDS_DITHER:
		    *val = atomDataPtr->LVDS_Info
			.LVDS_Info->ucLVDS_Misc & 0x40;
		    break;
		case     ATOM_LVDS_DUALLINK:
		    *val = atomDataPtr->LVDS_Info
			.LVDS_Info->ucLVDS_Misc & 0x01;
		    break;
		case     ATOM_LVDS_24BIT:
		    *val = atomDataPtr->LVDS_Info
			.LVDS_Info->ucLVDS_Misc & 0x02;
		    break;
		case     ATOM_LVDS_GREYLVL:
		    *val = atomDataPtr->LVDS_Info
			.LVDS_Info->ucLVDS_Misc & 0x0C;
		    break;
		case     ATOM_LVDS_FPDI:
		    *val = atomDataPtr->LVDS_Info
			.LVDS_Info->ucLVDS_Misc * 0x10;
		    break;
		default:
		    return ATOM_NOT_IMPLEMENTED;
	    }
	    break;
	case 2:
	    switch (func) {
		case ATOM_LVDS_SUPPORTED_REFRESH_RATE:
		    *val = atomDataPtr->LVDS_Info
			.LVDS_Info_v12->usSupportedRefreshRate;
		    break;
		case ATOM_LVDS_OFF_DELAY:
		    *val = atomDataPtr->LVDS_Info
			.LVDS_Info_v12->usOffDelayInMs;
		    break;
		case ATOM_LVDS_SEQ_DIG_ONTO_DE:
		    *val = atomDataPtr->LVDS_Info
			.LVDS_Info_v12->ucPowerSequenceDigOntoDEin10Ms * 10;
		    break;
		case ATOM_LVDS_SEQ_DE_TO_BL:
		    *val = atomDataPtr->LVDS_Info
			.LVDS_Info_v12->ucPowerSequenceDEtoBLOnin10Ms * 10;
		    break;
		case     ATOM_LVDS_DITHER:
		    *val = atomDataPtr->LVDS_Info
			.LVDS_Info_v12->ucLVDS_Misc & 0x40;
		    break;
		case     ATOM_LVDS_DUALLINK:
		    *val = atomDataPtr->LVDS_Info
			.LVDS_Info_v12->ucLVDS_Misc & 0x01;
		    break;
		case     ATOM_LVDS_24BIT:
		    *val = atomDataPtr->LVDS_Info
			.LVDS_Info_v12->ucLVDS_Misc & 0x02;
		    break;
		case     ATOM_LVDS_GREYLVL:
		    *val = atomDataPtr->LVDS_Info
			.LVDS_Info_v12->ucLVDS_Misc & 0x0C;
		    break;
		case     ATOM_LVDS_FPDI:
		    *val = atomDataPtr->LVDS_Info
			.LVDS_Info_v12->ucLVDS_Misc * 0x10;
		    break;
		default:
		    return ATOM_NOT_IMPLEMENTED;
	    }
	    break;
	default:
	    return ATOM_NOT_IMPLEMENTED;
    }

    return ATOM_SUCCESS;
}

static AtomBiosResult
rhdAtomCompassionateDataQuery(atomBiosHandlePtr handle,
			AtomBiosRequestID func, AtomBiosArgPtr data)
{
    atomDataTablesPtr atomDataPtr;
    CARD8 crev, frev;
    CARD32 *val = &data->val;

    //RHDFUNC(handle);

    atomDataPtr = handle->atomDataPtr;

    if (!rhdAtomGetTableRevisionAndSize(
	    (ATOM_COMMON_TABLE_HEADER *)(atomDataPtr->CompassionateData),
	    &frev,&crev,NULL)) {
	return ATOM_FAILED;
    }

    switch (func) {
	case ATOM_DAC1_BG_ADJ:
	    *val = atomDataPtr->CompassionateData->
		ucDAC1_BG_Adjustment;
	    break;
	case ATOM_DAC1_DAC_ADJ:
	    *val = atomDataPtr->CompassionateData->
		ucDAC1_DAC_Adjustment;
	    break;
	case ATOM_DAC1_FORCE:
	    *val = atomDataPtr->CompassionateData->
		usDAC1_FORCE_Data;
	    break;
	case ATOM_DAC2_CRTC2_BG_ADJ:
	    *val = atomDataPtr->CompassionateData->
		ucDAC2_CRT2_BG_Adjustment;
	    break;
	case ATOM_DAC2_CRTC2_DAC_ADJ:
	    *val = atomDataPtr->CompassionateData->
		ucDAC2_CRT2_DAC_Adjustment;
	    break;
	case ATOM_DAC2_CRTC2_FORCE:
	    *val = atomDataPtr->CompassionateData->
		usDAC2_CRT2_FORCE_Data;
	    break;
	case ATOM_DAC2_CRTC2_MUX_REG_IND:
	    *val = atomDataPtr->CompassionateData->
		usDAC2_CRT2_MUX_RegisterIndex;
	    break;
	case ATOM_DAC2_CRTC2_MUX_REG_INFO:
	    *val = atomDataPtr->CompassionateData->
		ucDAC2_CRT2_MUX_RegisterInfo;
	    break;
	default:
	    return ATOM_NOT_IMPLEMENTED;
    }
    return ATOM_SUCCESS;
}

static AtomBiosResult
rhdAtomGPIOI2CInfoQuery(atomBiosHandlePtr handle,
			AtomBiosRequestID func, AtomBiosArgPtr data)
{
    atomDataTablesPtr atomDataPtr;
    CARD8 crev, frev;
    CARD32 *val = &data->val;
    unsigned short size;

    //RHDFUNC(handle);

    atomDataPtr = handle->atomDataPtr;

    if (!rhdAtomGetTableRevisionAndSize(
	    (ATOM_COMMON_TABLE_HEADER *)(atomDataPtr->GPIO_I2C_Info),
	    &frev,&crev,&size)) {
	return ATOM_FAILED;
    }

    switch (func) {
	case ATOM_GPIO_I2C_CLK_MASK:
	    if ((sizeof(ATOM_COMMON_TABLE_HEADER)
		 + (*val * sizeof(ATOM_GPIO_I2C_ASSIGMENT))) > size) {
		xf86DrvMsg(handle->scrnIndex, X_ERROR, "%s: GPIO_I2C Device "
			   "num %lu exeeds table size %u\n",__func__,
			   (unsigned long)val,
			   size);
		return ATOM_FAILED;
	    }

	    *val = atomDataPtr->GPIO_I2C_Info->asGPIO_Info[*val]
		.usClkMaskRegisterIndex;
	    break;

	default:
	    return ATOM_NOT_IMPLEMENTED;
    }
    return ATOM_SUCCESS;
}

static AtomBiosResult
rhdAtomFirmwareInfoQuery(atomBiosHandlePtr handle,
			 AtomBiosRequestID func, AtomBiosArgPtr data)
{
    atomDataTablesPtr atomDataPtr;
    CARD8 crev, frev;
    CARD32 *val = &data->val;

    //RHDFUNC(handle);

    atomDataPtr = handle->atomDataPtr;

    if (!rhdAtomGetTableRevisionAndSize(
	    (ATOM_COMMON_TABLE_HEADER *)(atomDataPtr->FirmwareInfo.base),
	    &crev,&frev,NULL)) {
	return ATOM_FAILED;
    }

    switch (crev) {
	case 1:
	    switch (func) {
		case GET_DEFAULT_ENGINE_CLOCK:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo->ulDefaultEngineClock * 10;
		    break;
		case GET_DEFAULT_MEMORY_CLOCK:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo->ulDefaultMemoryClock * 10;
		    break;
		case GET_MAX_PIXEL_CLOCK_PLL_OUTPUT:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo->ulMaxPixelClockPLL_Output * 10;
		    break;
		case GET_MIN_PIXEL_CLOCK_PLL_OUTPUT:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo->usMinPixelClockPLL_Output * 10;
		case GET_MAX_PIXEL_CLOCK_PLL_INPUT:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo->usMaxPixelClockPLL_Input * 10;
		    break;
		case GET_MIN_PIXEL_CLOCK_PLL_INPUT:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo->usMinPixelClockPLL_Input * 10;
		    break;
		case GET_MAX_PIXEL_CLK:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo->usMaxPixelClock * 10;
		    break;
		case GET_REF_CLOCK:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo->usReferenceClock * 10;
		    break;
		default:
		    return ATOM_NOT_IMPLEMENTED;
	    }
	case 2:
	    switch (func) {
		case GET_DEFAULT_ENGINE_CLOCK:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo_V_1_2->ulDefaultEngineClock * 10;
		    break;
		case GET_DEFAULT_MEMORY_CLOCK:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo_V_1_2->ulDefaultMemoryClock * 10;
		    break;
		case GET_MAX_PIXEL_CLOCK_PLL_OUTPUT:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo_V_1_2->ulMaxPixelClockPLL_Output * 10;
		    break;
		case GET_MIN_PIXEL_CLOCK_PLL_OUTPUT:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo_V_1_2->usMinPixelClockPLL_Output * 10;
		    break;
		case GET_MAX_PIXEL_CLOCK_PLL_INPUT:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo_V_1_2->usMaxPixelClockPLL_Input * 10;
		    break;
		case GET_MIN_PIXEL_CLOCK_PLL_INPUT:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo_V_1_2->usMinPixelClockPLL_Input * 10;
		    break;
		case GET_MAX_PIXEL_CLK:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo_V_1_2->usMaxPixelClock * 10;
		    break;
		case GET_REF_CLOCK:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo_V_1_2->usReferenceClock * 10;
		    break;
		default:
		    return ATOM_NOT_IMPLEMENTED;
	    }
	    break;
	case 3:
	    switch (func) {
		case GET_DEFAULT_ENGINE_CLOCK:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo_V_1_3->ulDefaultEngineClock * 10;
		    break;
		case GET_DEFAULT_MEMORY_CLOCK:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo_V_1_3->ulDefaultMemoryClock * 10;
		    break;
		case GET_MAX_PIXEL_CLOCK_PLL_OUTPUT:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo_V_1_3->ulMaxPixelClockPLL_Output * 10;
		    break;
		case GET_MIN_PIXEL_CLOCK_PLL_OUTPUT:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo_V_1_3->usMinPixelClockPLL_Output * 10;
		    break;
		case GET_MAX_PIXEL_CLOCK_PLL_INPUT:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo_V_1_3->usMaxPixelClockPLL_Input * 10;
		    break;
		case GET_MIN_PIXEL_CLOCK_PLL_INPUT:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo_V_1_3->usMinPixelClockPLL_Input * 10;
		    break;
		case GET_MAX_PIXEL_CLK:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo_V_1_3->usMaxPixelClock * 10;
		    break;
		case GET_REF_CLOCK:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo_V_1_3->usReferenceClock * 10;
		    break;
		default:
		    return ATOM_NOT_IMPLEMENTED;
	    }
	    break;
	case 4:
	    switch (func) {
		case GET_DEFAULT_ENGINE_CLOCK:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo_V_1_4->ulDefaultEngineClock * 10;
		    break;
		case GET_DEFAULT_MEMORY_CLOCK:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo_V_1_4->ulDefaultMemoryClock * 10;
		    break;
		case GET_MAX_PIXEL_CLOCK_PLL_INPUT:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo_V_1_4->usMaxPixelClockPLL_Input * 10;
		    break;
		case GET_MIN_PIXEL_CLOCK_PLL_INPUT:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo_V_1_4->usMinPixelClockPLL_Input * 10;
		    break;
		case GET_MAX_PIXEL_CLOCK_PLL_OUTPUT:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo_V_1_4->ulMaxPixelClockPLL_Output * 10;
		    break;
		case GET_MIN_PIXEL_CLOCK_PLL_OUTPUT:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo_V_1_4->usMinPixelClockPLL_Output * 10;
		    break;
		case GET_MAX_PIXEL_CLK:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo_V_1_4->usMaxPixelClock * 10;
		    break;
		case GET_REF_CLOCK:
		    *val = atomDataPtr->FirmwareInfo
			.FirmwareInfo_V_1_4->usReferenceClock * 10;
		    break;
		default:
		    return ATOM_NOT_IMPLEMENTED;
	    }
	    break;
	default:
	    return ATOM_NOT_IMPLEMENTED;
    }
    return ATOM_SUCCESS;
}

const int object_connector_convert[] =
    { CONNECTOR_NONE,
      CONNECTOR_DVI_I,
      CONNECTOR_DVI_I,
      CONNECTOR_DVI_D,
      CONNECTOR_DVI_D,
      CONNECTOR_VGA,
      CONNECTOR_CTV,
      CONNECTOR_STV,
      CONNECTOR_NONE,
      CONNECTOR_DIN,
      CONNECTOR_SCART,
      CONNECTOR_HDMI_TYPE_A,
      CONNECTOR_HDMI_TYPE_B,
      CONNECTOR_HDMI_TYPE_B,
      CONNECTOR_LVDS,
      CONNECTOR_DIN,
      CONNECTOR_NONE,
      CONNECTOR_NONE,
      CONNECTOR_NONE,
      CONNECTOR_NONE,
    };
     
static void
rhdAtomParseI2CRecord(atomBiosHandlePtr handle,
			ATOM_I2C_RECORD *Record, CARD32 *ddc_line)
{
    ErrorF(" %s:  I2C Record: %s[%x] EngineID: %x I2CAddr: %x\n",
	     __func__,
	     Record->sucI2cId.bfHW_Capable ? "HW_Line" : "GPIO_ID",
	     Record->sucI2cId.bfI2C_LineMux,
	     Record->sucI2cId.bfHW_EngineID,
	     Record->ucI2CAddr);

    if (!*(unsigned char *)&(Record->sucI2cId))
	*ddc_line = 0;
    else {

	if (Record->ucI2CAddr != 0)
	    return;

	if (Record->sucI2cId.bfHW_Capable) {
	    switch(Record->sucI2cId.bfI2C_LineMux) {
	    case 0: *ddc_line = 0x7e40; break;
	    case 1: *ddc_line = 0x7e50; break;
	    case 2: *ddc_line = 0x7e30; break;
	    default: break;
	    }
	    return;
	
	} else {
	    /* add GPIO pin parsing */
	}
    }
}

static CARD32
RADEONLookupGPIOLineForDDC(ScrnInfoPtr pScrn, CARD8 id)
{
    RADEONInfoPtr info = RADEONPTR (pScrn);
    atomDataTablesPtr atomDataPtr;
    ATOM_GPIO_I2C_ASSIGMENT gpio;
    CARD32 ret = 0;
    CARD8 crev, frev;

    atomDataPtr = info->atomBIOS->atomDataPtr;

    if (!rhdAtomGetTableRevisionAndSize(
	    &(atomDataPtr->GPIO_I2C_Info->sHeader),
	    &crev,&frev,NULL)) {
	xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "No GPIO Info Table found!\n");
	return ret;
    }

    /* note clk and data regs can be different!
     * gpio.usClkMaskRegisterIndex and gpio.usDataMaskRegisterIndex
     */

    gpio = atomDataPtr->GPIO_I2C_Info->asGPIO_Info[id];
    ret = gpio.usClkMaskRegisterIndex * 4;

    return ret;
}

Bool
RADEONGetATOMConnectorInfoFromBIOSObject (ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info = RADEONPTR (pScrn);
    CARD8 crev, frev;
    unsigned short size;
    atomDataTablesPtr atomDataPtr;
    ATOM_CONNECTOR_OBJECT_TABLE *con_obj;
    int i, j;

    atomDataPtr = info->atomBIOS->atomDataPtr;
    if (!rhdAtomGetTableRevisionAndSize((ATOM_COMMON_TABLE_HEADER *)(atomDataPtr->Object_Header), &crev, &frev, &size))
	return FALSE;

    if (crev < 2)
	return FALSE;
    
    con_obj = (ATOM_CONNECTOR_OBJECT_TABLE *)
	((char *)&atomDataPtr->Object_Header->sHeader +
	 atomDataPtr->Object_Header->usConnectorObjectTableOffset);

    for (i = 0; i < con_obj->ucNumberOfObjects; i++) {
	ATOM_SRC_DST_TABLE_FOR_ONE_OBJECT *SrcDstTable;
	ATOM_COMMON_RECORD_HEADER *Record;
	CARD8 obj_id, num, obj_type;
	int record_base;

	obj_id = (con_obj->asObjects[i].usObjectID & OBJECT_ID_MASK) >> OBJECT_ID_SHIFT;
	num = (con_obj->asObjects[i].usObjectID & ENUM_ID_MASK) >> ENUM_ID_SHIFT;
	obj_type = (con_obj->asObjects[i].usObjectID & OBJECT_TYPE_MASK) >> OBJECT_TYPE_SHIFT;
	if (obj_type != GRAPH_OBJECT_TYPE_CONNECTOR)
	    continue;

	SrcDstTable = (ATOM_SRC_DST_TABLE_FOR_ONE_OBJECT *)
	    ((char *)&atomDataPtr->Object_Header->sHeader
	     + con_obj->asObjects[i].usSrcDstTableOffset);
	
	ErrorF("object id %04x %02x\n", obj_id, SrcDstTable->ucNumberOfSrc);
	info->BiosConnector[i].ConnectorType = object_connector_convert[obj_id];
	info->BiosConnector[i].valid = TRUE;
	info->BiosConnector[i].devices = 0;

	for (j = 0; j < SrcDstTable->ucNumberOfSrc; j++) {
	    CARD8 sobj_id;

	    sobj_id = (SrcDstTable->usSrcObjectID[j] & OBJECT_ID_MASK) >> OBJECT_ID_SHIFT;
	    ErrorF("src object id %04x %d\n", SrcDstTable->usSrcObjectID[j], sobj_id);
	    
	    switch(sobj_id) {
	    case ENCODER_OBJECT_ID_INTERNAL_LVDS:
		info->BiosConnector[i].devices |= (1 << ATOM_DEVICE_LCD1_INDEX);
		break;
	    case ENCODER_OBJECT_ID_INTERNAL_TMDS1:
		info->BiosConnector[i].devices |= (1 << ATOM_DEVICE_DFP1_INDEX);
		info->BiosConnector[i].TMDSType = TMDS_INT;
		break;
	    case ENCODER_OBJECT_ID_INTERNAL_TMDS2:
		info->BiosConnector[i].devices |= (1 << ATOM_DEVICE_DFP2_INDEX);
		info->BiosConnector[i].TMDSType = TMDS_EXT;
		break;
	    case ENCODER_OBJECT_ID_INTERNAL_LVTM1:
		info->BiosConnector[i].devices |= (1 << ATOM_DEVICE_DFP3_INDEX);
		info->BiosConnector[i].TMDSType = TMDS_EXT;
		break;
	    case ENCODER_OBJECT_ID_INTERNAL_DAC1:
	    case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC1:
		info->BiosConnector[i].devices |= (1 << ATOM_DEVICE_CRT1_INDEX);
		info->BiosConnector[i].DACType = DAC_PRIMARY;
		break;
	    case ENCODER_OBJECT_ID_INTERNAL_DAC2:
	    case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC2:
		info->BiosConnector[i].devices |= (1 << ATOM_DEVICE_CRT2_INDEX);
		info->BiosConnector[i].DACType = DAC_TVDAC;
		break;
	    }
	}

	Record = (ATOM_COMMON_RECORD_HEADER *)
	    ((char *)&atomDataPtr->Object_Header->sHeader
	     + con_obj->asObjects[i].usRecordOffset);

	record_base = con_obj->asObjects[i].usRecordOffset;

	while (Record->ucRecordType > 0
	       && Record->ucRecordType <= ATOM_MAX_OBJECT_RECORD_NUMBER ) {

	    ErrorF("record type %d\n", Record->ucRecordType);
	    switch (Record->ucRecordType) {
		case ATOM_I2C_RECORD_TYPE:
		    rhdAtomParseI2CRecord(&info->atomBIOS, 
					  (ATOM_I2C_RECORD *)Record,
					  &info->BiosConnector[i].ddc_line);
		    break;
		case ATOM_HPD_INT_RECORD_TYPE:
		    break;
		case ATOM_CONNECTOR_DEVICE_TAG_RECORD_TYPE:
		    break;
	    }

	    Record = (ATOM_COMMON_RECORD_HEADER*)
		((char *)Record + Record->ucRecordSize);
	}
    }
    return TRUE;
}


Bool
RADEONGetATOMConnectorInfoFromBIOSConnectorTable (ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info = RADEONPTR (pScrn);
    atomDataTablesPtr atomDataPtr;
    CARD8 crev, frev;
    int i, j;

    atomDataPtr = info->atomBIOS->atomDataPtr;

    if (!rhdAtomGetTableRevisionAndSize(
	    &(atomDataPtr->SupportedDevicesInfo.SupportedDevicesInfo->sHeader),
	    &crev,&frev,NULL)) {
	xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "No Device Info Table found!\n");
	return FALSE;
    }

    for (i = 0; i < ATOM_MAX_SUPPORTED_DEVICE; i++) {
	ATOM_CONNECTOR_INFO_I2C ci
	    = atomDataPtr->SupportedDevicesInfo.SupportedDevicesInfo->asConnInfo[i];

	if (!(atomDataPtr->SupportedDevicesInfo
	      .SupportedDevicesInfo->usDeviceSupport & (1 << i))) {
	    info->BiosConnector[i].valid = FALSE;
	    continue;
	}

	if (i == ATOM_DEVICE_CV_INDEX) {
	    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Skipping Component Video\n");
	    info->BiosConnector[i].valid = FALSE;
	    continue;
	}

	info->BiosConnector[i].valid = TRUE;
	info->BiosConnector[i].output_id = ci.sucI2cId.sbfAccess.bfI2C_LineMux;
	info->BiosConnector[i].devices = (1 << i);
	info->BiosConnector[i].ConnectorType = ci.sucConnectorInfo.sbfAccess.bfConnectorType;
	info->BiosConnector[i].DACType = ci.sucConnectorInfo.sbfAccess.bfAssociatedDAC - 1;

	if (ci.sucI2cId.sbfAccess.bfHW_Capable) {
	    /* don't assign a gpio for tv */
	    if ((i == ATOM_DEVICE_TV1_INDEX) ||
		(i == ATOM_DEVICE_TV2_INDEX) ||
		(i == ATOM_DEVICE_CV_INDEX))
		info->BiosConnector[i].ddc_line = 0;
	    else
		info->BiosConnector[i].ddc_line =
		    RADEONLookupGPIOLineForDDC(pScrn, ci.sucI2cId.sbfAccess.bfI2C_LineMux);
	} else if (ci.sucI2cId.sbfAccess.bfI2C_LineMux) {
	    /* add support for GPIO line */
	    ErrorF("Unsupported SW GPIO - device %d: gpio line: 0x%x\n",
		   i, RADEONLookupGPIOLineForDDC(pScrn, ci.sucI2cId.sbfAccess.bfI2C_LineMux));
	    info->BiosConnector[i].ddc_line = 0;
	} else {
	    info->BiosConnector[i].ddc_line = 0;
	}

	if (i == ATOM_DEVICE_DFP1_INDEX)
	    info->BiosConnector[i].TMDSType = TMDS_INT;
	else if (i == ATOM_DEVICE_DFP2_INDEX)
	    info->BiosConnector[i].TMDSType = TMDS_EXT;
	else if (i == ATOM_DEVICE_DFP3_INDEX)
	    info->BiosConnector[i].TMDSType = TMDS_EXT;
	else
	    info->BiosConnector[i].TMDSType = TMDS_UNKNOWN;

	/* Always set the connector type to VGA for CRT1/CRT2. if they are
	 * shared with a DVI port, we'll pick up the DVI connector below when we
	 * merge the outputs
	 */
	if ((i == ATOM_DEVICE_CRT1_INDEX || i == ATOM_DEVICE_CRT2_INDEX) &&
	    (info->BiosConnector[i].ConnectorType == CONNECTOR_DVI_I ||
	     info->BiosConnector[i].ConnectorType == CONNECTOR_DVI_D ||
	     info->BiosConnector[i].ConnectorType == CONNECTOR_DVI_A)) {
	    info->BiosConnector[i].ConnectorType = CONNECTOR_VGA;
	}

	if (crev > 1) {
	    ATOM_CONNECTOR_INC_SRC_BITMAP isb
		= atomDataPtr->SupportedDevicesInfo
		.SupportedDevicesInfo_HD->asIntSrcInfo[i];

	    switch (isb.ucIntSrcBitmap) {
		case 0x4:
		    info->BiosConnector[i].hpd_mask = 0x00000001;
		    break;
		case 0xa:
		    info->BiosConnector[i].hpd_mask = 0x00000100;
		    break;
		default:
		    info->BiosConnector[i].hpd_mask = 0;
		    break;
	    }
	} else {
	    info->BiosConnector[i].hpd_mask = 0;
	}
    }

    /* CRTs/DFPs may share a port */
    for (i = 0; i < ATOM_MAX_SUPPORTED_DEVICE; i++) {
	if (info->BiosConnector[i].valid) {
	    for (j = 0; j < ATOM_MAX_SUPPORTED_DEVICE; j++) {
		if (info->BiosConnector[j].valid && (i != j) ) {
		    if (info->BiosConnector[i].output_id == info->BiosConnector[j].output_id) {
			if (((i == ATOM_DEVICE_DFP1_INDEX) ||
			     (i == ATOM_DEVICE_DFP2_INDEX) ||
			     (i == ATOM_DEVICE_DFP3_INDEX)) &&
			    ((j == ATOM_DEVICE_CRT1_INDEX) || (j == ATOM_DEVICE_CRT2_INDEX))) {
			    info->BiosConnector[i].DACType = info->BiosConnector[j].DACType;
			    info->BiosConnector[i].devices |= info->BiosConnector[j].devices;
			    info->BiosConnector[j].valid = FALSE;
			} else if (((j == ATOM_DEVICE_DFP1_INDEX) ||
			     (j == ATOM_DEVICE_DFP2_INDEX) ||
			     (j == ATOM_DEVICE_DFP3_INDEX)) &&
			    ((i == ATOM_DEVICE_CRT1_INDEX) || (i == ATOM_DEVICE_CRT2_INDEX))) {
			    info->BiosConnector[j].DACType = info->BiosConnector[i].DACType;
			    info->BiosConnector[j].devices |= info->BiosConnector[i].devices;
			    info->BiosConnector[i].valid = FALSE;
			}
			/* other possible combos?  */
		    }
		}
	    }
	}
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Bios Connector table: \n");
    for (i = 0; i < ATOM_MAX_SUPPORTED_DEVICE; i++) {
	if (info->BiosConnector[i].valid) {
	    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Port%d: DDCType-0x%x, DACType-%d, TMDSType-%d, ConnectorType-%d, hpd_mask-0x%x\n",
		       i, info->BiosConnector[i].ddc_line, info->BiosConnector[i].DACType,
		       info->BiosConnector[i].TMDSType, info->BiosConnector[i].ConnectorType,
		       info->BiosConnector[i].hpd_mask);
	}
    }

    return TRUE;
}

#if 0
#define RHD_CONNECTORS_MAX 4
#define MAX_OUTPUTS_PER_CONNECTOR 2

#define Limit(n,max,name) ((n >= max) ? ( \
     xf86DrvMsg(handle->scrnIndex,X_ERROR,"%s: %s %i exceeds maximum %i\n", \
		__func__,name,n,max), TRUE) : FALSE)

static const struct _rhd_connector_objs
{
    char *name;
    RADEONConnectorTypeATOM con;
} rhd_connector_objs[] = {
    { "NONE", CONNECTOR_NONE_ATOM },
    { "SINGLE_LINK_DVI_I", CONNECTOR_DVI_I_ATOM },
    { "DUAL_LINK_DVI_I", CONNECTOR_DVI_I_ATOM },
    { "SINGLE_LINK_DVI_D", CONNECTOR_DVI_D_ATOM },
    { "DUAL_LINK_DVI_D", CONNECTOR_DVI_D_ATOM },
    { "VGA", CONNECTOR_VGA_ATOM },
    { "COMPOSITE", CONNECTOR_CTV_ATOM },
    { "SVIDEO", CONNECTOR_STV_ATOM },
    { "D_CONNECTOR", CONNECTOR_NONE_ATOM },
    { "9PIN_DIN", CONNECTOR_NONE_ATOM },
    { "SCART", CONNECTOR_SCART_ATOM },
    { "HDMI_TYPE_A", CONNECTOR_HDMI_TYPE_A_ATOM },
    { "HDMI_TYPE_B", CONNECTOR_HDMI_TYPE_B_ATOM },
    { "HDMI_TYPE_B", CONNECTOR_HDMI_TYPE_B_ATOM },
    { "LVDS", CONNECTOR_LVDS_ATOM },
    { "7PIN_DIN", CONNECTOR_STV_ATOM },
    { "PCIE_CONNECTOR", CONNECTOR_NONE_ATOM },
    { "CROSSFIRE", CONNECTOR_NONE_ATOM },
    { "HARDCODE_DVI", CONNECTOR_NONE_ATOM },
    { "DISPLAYPORT", CONNECTOR_DISPLAY_PORT_ATOM }
};
static const int n_rhd_connector_objs = sizeof (rhd_connector_objs) / sizeof(struct _rhd_connector_objs);

static const struct _rhd_encoders
{
    char *name;
    RADEONOutputTypeATOM ot;
} rhd_encoders[] = {
    { "NONE", OUTPUT_NONE_ATOM },
    { "INTERNAL_LVDS", OUTPUT_LVDS_ATOM },
    { "INTERNAL_TMDS1", OUTPUT_TMDSA_ATOM },
    { "INTERNAL_TMDS2", OUTPUT_TMDSB_ATOM },
    { "INTERNAL_DAC1", OUTPUT_DACA_ATOM },
    { "INTERNAL_DAC2", OUTPUT_DACB_ATOM },
    { "INTERNAL_SDVOA", OUTPUT_NONE_ATOM },
    { "INTERNAL_SDVOB", OUTPUT_NONE_ATOM },
    { "SI170B", OUTPUT_NONE_ATOM },
    { "CH7303", OUTPUT_NONE_ATOM },
    { "CH7301", OUTPUT_NONE_ATOM },
    { "INTERNAL_DVO1", OUTPUT_NONE_ATOM },
    { "EXTERNAL_SDVOA", OUTPUT_NONE_ATOM },
    { "EXTERNAL_SDVOB", OUTPUT_NONE_ATOM },
    { "TITFP513", OUTPUT_NONE_ATOM },
    { "INTERNAL_LVTM1", OUTPUT_LVTMA_ATOM },
    { "VT1623", OUTPUT_NONE_ATOM },
    { "HDMI_SI1930", OUTPUT_NONE_ATOM },
    { "HDMI_INTERNAL", OUTPUT_NONE_ATOM },
    { "INTERNAL_KLDSCP_TMDS1", OUTPUT_TMDSA_ATOM },
    { "INTERNAL_KLSCP_DVO1", OUTPUT_NONE_ATOM },
    { "INTERNAL_KLDSCP_DAC1", OUTPUT_DACA_ATOM },
    { "INTERNAL_KLDSCP_DAC2", OUTPUT_DACB_ATOM },
    { "SI178", OUTPUT_NONE_ATOM },
    { "MVPU_FPGA", OUTPUT_NONE_ATOM },
    { "INTERNAL_DDI", OUTPUT_NONE_ATOM },
    { "VT1625", OUTPUT_NONE_ATOM },
    { "HDMI_SI1932", OUTPUT_NONE_ATOM },
    { "AN9801", OUTPUT_NONE_ATOM },
    { "DP501",  OUTPUT_NONE_ATOM },
};
static const int n_rhd_encoders = sizeof (rhd_encoders) / sizeof(struct _rhd_encoders);

static const struct _rhd_connectors
{
    char *name;
    RADEONConnectorTypeATOM con;
    Bool dual;
} rhd_connectors[] = {
    {"NONE", CONNECTOR_NONE_ATOM, FALSE },
    {"VGA", CONNECTOR_VGA_ATOM, FALSE },
    {"DVI-I", CONNECTOR_DVI_I_ATOM, TRUE },
    {"DVI-D", CONNECTOR_DVI_D_ATOM, FALSE },
    {"DVI-A", CONNECTOR_DVI_A_ATOM, FALSE },
    {"SVIDEO", CONNECTOR_STV_ATOM, FALSE },
    {"COMPOSITE", CONNECTOR_CTV_ATOM, FALSE },
    {"PANEL", CONNECTOR_LVDS_ATOM, FALSE },
    {"DIGITAL_LINK", CONNECTOR_DIGITAL_ATOM, FALSE },
    {"SCART", CONNECTOR_SCART_ATOM, FALSE },
    {"HDMI Type A", CONNECTOR_HDMI_TYPE_A_ATOM, FALSE },
    {"HDMI Type B", CONNECTOR_HDMI_TYPE_B_ATOM, FALSE },
    {"UNKNOWN", CONNECTOR_NONE_ATOM, FALSE },
    {"UNKNOWN", CONNECTOR_NONE_ATOM, FALSE },
    {"DVI+DIN", CONNECTOR_NONE_ATOM, FALSE }
};
static const int n_rhd_connectors = sizeof(rhd_connectors) / sizeof(struct _rhd_connectors);

static const struct _rhd_devices
{
    char *name;
    RADEONOutputTypeATOM ot;
} rhd_devices[] = {
    {" CRT1", OUTPUT_NONE_ATOM },
    {" LCD1", OUTPUT_LVTMA_ATOM },
    {" TV1", OUTPUT_NONE_ATOM },
    {" DFP1", OUTPUT_TMDSA_ATOM },
    {" CRT2", OUTPUT_NONE_ATOM },
    {" LCD2", OUTPUT_LVTMA_ATOM },
    {" TV2", OUTPUT_NONE_ATOM },
    {" DFP2", OUTPUT_LVTMA_ATOM },
    {" CV", OUTPUT_NONE_ATOM },
    {" DFP3", OUTPUT_LVTMA_ATOM }
};
static const int n_rhd_devices = sizeof(rhd_devices) / sizeof(struct _rhd_devices);

static const rhdDDC hwddc[] = { RHD_DDC_0, RHD_DDC_1, RHD_DDC_2, RHD_DDC_3 };
static const int n_hwddc = sizeof(hwddc) / sizeof(rhdDDC);

static const rhdOutputType acc_dac[] = { OUTPUT_NONE_ATOM,
					 OUTPUT_DACA_ATOM,
					 OUTPUT_DACB_ATOM,
					 OUTPUT_DAC_EXTERNAL_ATOM };
static const int n_acc_dac = sizeof(acc_dac) / sizeof (rhdOutputType);

/*
 *
 */
static Bool
rhdAtomInterpretObjectID(atomBiosHandlePtr handle,
			 CARD16 id, CARD8 *obj_type, CARD8 *obj_id,
			 CARD8 *num, char **name)
{
    *obj_id = (id & OBJECT_ID_MASK) >> OBJECT_ID_SHIFT;
    *num = (id & ENUM_ID_MASK) >> ENUM_ID_SHIFT;
    *obj_type = (id & OBJECT_TYPE_MASK) >> OBJECT_TYPE_SHIFT;

    *name = NULL;

    switch (*obj_type) {
	case GRAPH_OBJECT_TYPE_CONNECTOR:
	    if (!Limit(*obj_id, n_rhd_connector_objs, "obj_id"))
		*name = rhd_connector_objs[*obj_id].name;
	    break;
	case GRAPH_OBJECT_TYPE_ENCODER:
	    if (!Limit(*obj_id, n_rhd_encoders, "obj_id"))
		*name = rhd_encoders[*obj_id].name;
	    break;
	default:
	    break;
    }
    return TRUE;
}

/*
 *
 */
static void
rhdAtomDDCFromI2CRecord(atomBiosHandlePtr handle,
			ATOM_I2C_RECORD *Record, rhdDDC *DDC)
{
    RHDDebug(handle->scrnIndex,
	     "   %s:  I2C Record: %s[%x] EngineID: %x I2CAddr: %x\n",
	     __func__,
	     Record->sucI2cId.bfHW_Capable ? "HW_Line" : "GPIO_ID",
	     Record->sucI2cId.bfI2C_LineMux,
	     Record->sucI2cId.bfHW_EngineID,
	     Record->ucI2CAddr);

    if (!*(unsigned char *)&(Record->sucI2cId))
	*DDC = RHD_DDC_NONE;
    else {

	if (Record->ucI2CAddr != 0)
	    return;

	if (Record->sucI2cId.bfHW_Capable) {

	    *DDC = (rhdDDC)Record->sucI2cId.bfI2C_LineMux;
	    if (*DDC >= RHD_DDC_MAX)
		*DDC = RHD_DDC_NONE;

	} else {
	    *DDC = RHD_DDC_GPIO;
	    /* add GPIO pin parsing */
	}
    }
}

/*
 *
 */
static void
rhdAtomParseGPIOLutForHPD(atomBiosHandlePtr handle,
			  CARD8 pinID, rhdHPD *HPD)
{
    atomDataTablesPtr atomDataPtr;
    ATOM_GPIO_PIN_LUT *gpio_pin_lut;
    unsigned short size;
    int i = 0;

    //RHDFUNC(handle);

    atomDataPtr = handle->atomDataPtr;

    *HPD = RHD_HPD_NONE;

    if (!rhdAtomGetTableRevisionAndSize(
	    &atomDataPtr->GPIO_Pin_LUT->sHeader, NULL, NULL, &size)) {
	xf86DrvMsg(handle->scrnIndex, X_ERROR,
		   "%s: No valid GPIO pin LUT in AtomBIOS\n",__func__);
	return;
    }
    gpio_pin_lut = atomDataPtr->GPIO_Pin_LUT;

    while (1) {
	if (gpio_pin_lut->asGPIO_Pin[i].ucGPIO_ID  == pinID) {

	    if ((sizeof(ATOM_COMMON_TABLE_HEADER)
		  + (i * sizeof(ATOM_GPIO_PIN_ASSIGNMENT))) > size)
		return;

	    RHDDebug(handle->scrnIndex,
		     "   %s: GPIO PinID: %i Index: %x Shift: %i\n",
		     __func__,
		     pinID,
		     gpio_pin_lut->asGPIO_Pin[i].usGpioPin_AIndex,
		     gpio_pin_lut->asGPIO_Pin[i].ucGpioPinBitShift);

	    /* grr... map backwards: register indices -> line numbers */
	    if (gpio_pin_lut->asGPIO_Pin[i].usGpioPin_AIndex
		== (DC_GPIO_HPD_A >> 2)) {
		switch (gpio_pin_lut->asGPIO_Pin[i].ucGpioPinBitShift) {
		    case 0:
			*HPD = RHD_HPD_0;
			return;
		    case 8:
			*HPD = RHD_HPD_1;
			return;
		    case 16:
			*HPD = RHD_HPD_2;
			return;
		}
	    }
	}
	i++;
    }
}

/*
 *
 */
static void
rhdAtomHPDFromRecord(atomBiosHandlePtr handle,
		     ATOM_HPD_INT_RECORD *Record, rhdHPD *HPD)
{
    RHDDebug(handle->scrnIndex,
	     "   %s:  HPD Record: GPIO ID: %x Plugged_PinState: %x\n",
	     __func__,
	     Record->ucHPDIntGPIOID,
	     Record->ucPluggged_PinState);
    rhdAtomParseGPIOLutForHPD(handle, Record->ucHPDIntGPIOID, HPD);
}

/*
 *
 */
static char *
rhdAtomDeviceTagsFromRecord(atomBiosHandlePtr handle,
			    ATOM_CONNECTOR_DEVICE_TAG_RECORD *Record)
{
    int i, j, k;
    char *devices;

    //RHDFUNC(handle);

    RHDDebug(handle->scrnIndex,"   NumberOfDevice: %i\n",
	     Record->ucNumberOfDevice);

    if (!Record->ucNumberOfDevice) return NULL;

    devices = (char *)xcalloc(Record->ucNumberOfDevice * 4 + 1,1);

    for (i = 0; i < Record->ucNumberOfDevice; i++) {
	k = 0;
	j = Record->asDeviceTag[i].usDeviceID;

	while (!(j & 0x1)) { j >>= 1; k++; };

	if (!Limit(k,n_rhd_devices,"usDeviceID"))
	    strcat(devices, rhd_devices[k].name);
    }

    RHDDebug(handle->scrnIndex,"   Devices:%s\n",devices);

    return devices;
}

/*
 *
 */
static AtomBiosResult
rhdAtomConnectorInfoFromObjectHeader(atomBiosHandlePtr handle,
				     rhdConnectorInfoPtr *ptr)
{
    atomDataTablesPtr atomDataPtr;
    CARD8 crev, frev;
    ATOM_CONNECTOR_OBJECT_TABLE *con_obj;
    rhdConnectorInfoPtr cp;
    unsigned long object_header_end;
    int ncon = 0;
    int i,j;
    unsigned short object_header_size;

    //RHDFUNC(handle);

    atomDataPtr = handle->atomDataPtr;

    if (!rhdAtomGetTableRevisionAndSize(
	    &atomDataPtr->Object_Header->sHeader,
	    &crev,&frev,&object_header_size)) {
	return ATOM_NOT_IMPLEMENTED;
    }

    if (crev < 2) /* don't bother with anything below rev 2 */
	return ATOM_NOT_IMPLEMENTED;

    if (!(cp = (rhdConnectorInfoPtr)xcalloc(sizeof(struct rhdConnectorInfo),
					 RHD_CONNECTORS_MAX)))
	return ATOM_FAILED;

    object_header_end =
	atomDataPtr->Object_Header->usConnectorObjectTableOffset
	+ object_header_size;

    RHDDebug(handle->scrnIndex,"ObjectTable - size: %u, BIOS - size: %u "
	     "TableOffset: %u object_header_end: %u\n",
	     object_header_size, handle->BIOSImageSize,
	     atomDataPtr->Object_Header->usConnectorObjectTableOffset,
	     object_header_end);

    if ((object_header_size > handle->BIOSImageSize)
	|| (atomDataPtr->Object_Header->usConnectorObjectTableOffset
	    > handle->BIOSImageSize)
	|| object_header_end > handle->BIOSImageSize) {
	xf86DrvMsg(handle->scrnIndex, X_ERROR,
		   "%s: Object table information is bogus\n",__func__);
	return ATOM_FAILED;
    }

    if (((unsigned long)&atomDataPtr->Object_Header->sHeader
	 + object_header_end) >  ((unsigned long)handle->BIOSBase
		     + handle->BIOSImageSize)) {
	xf86DrvMsg(handle->scrnIndex, X_ERROR,
		   "%s: Object table extends beyond BIOS Image\n",__func__);
	return ATOM_FAILED;
    }

    con_obj = (ATOM_CONNECTOR_OBJECT_TABLE *)
	((char *)&atomDataPtr->Object_Header->sHeader +
	 atomDataPtr->Object_Header->usConnectorObjectTableOffset);

    for (i = 0; i < con_obj->ucNumberOfObjects; i++) {
	ATOM_SRC_DST_TABLE_FOR_ONE_OBJECT *SrcDstTable;
	ATOM_COMMON_RECORD_HEADER *Record;
	int record_base;
	CARD8 obj_type, obj_id, num;
	char *name;
	int nout = 0;

	rhdAtomInterpretObjectID(handle, con_obj->asObjects[i].usObjectID,
			     &obj_type, &obj_id, &num, &name);

	RHDDebug(handle->scrnIndex, "Object: ID: %x name: %s type: %x id: %x\n",
		 con_obj->asObjects[i].usObjectID, name ? name : "",
		 obj_type, obj_id);


	if (obj_type != GRAPH_OBJECT_TYPE_CONNECTOR)
	    continue;

	SrcDstTable = (ATOM_SRC_DST_TABLE_FOR_ONE_OBJECT *)
	    ((char *)&atomDataPtr->Object_Header->sHeader
	     + con_obj->asObjects[i].usSrcDstTableOffset);

	if (con_obj->asObjects[i].usSrcDstTableOffset
	    + (SrcDstTable->ucNumberOfSrc
	       * sizeof(ATOM_SRC_DST_TABLE_FOR_ONE_OBJECT))
	    > handle->BIOSImageSize) {
	    xf86DrvMsg(handle->scrnIndex, X_ERROR, "%s: SrcDstTable[%i] extends "
		       "beyond Object_Header table\n",__func__,i);
	    continue;
	}

	cp[ncon].Type = rhd_connector_objs[obj_id].con;
	cp[ncon].Name = RhdAppendString(cp[ncon].Name,name);

	for (j = 0; j < SrcDstTable->ucNumberOfSrc; j++) {
	    CARD8 stype, sobj_id, snum;
	    char *sname;

	    rhdAtomInterpretObjectID(handle, SrcDstTable->usSrcObjectID[j],
				     &stype, &sobj_id, &snum, &sname);

	    RHDDebug(handle->scrnIndex, " * SrcObject: ID: %x name: %s\n",
		     SrcDstTable->usSrcObjectID[j], sname);

	    cp[ncon].Output[nout] = rhd_encoders[sobj_id].ot;
	    if (++nout >= MAX_OUTPUTS_PER_CONNECTOR)
		break;
	}

	Record = (ATOM_COMMON_RECORD_HEADER *)
	    ((char *)&atomDataPtr->Object_Header->sHeader
	     + con_obj->asObjects[i].usRecordOffset);

	record_base = con_obj->asObjects[i].usRecordOffset;

	while (Record->ucRecordType > 0
	       && Record->ucRecordType <= ATOM_MAX_OBJECT_RECORD_NUMBER ) {
	    char *taglist;

	    if ((record_base += Record->ucRecordSize)
		> object_header_size) {
		xf86DrvMsg(handle->scrnIndex, X_ERROR,
			   "%s: Object Records extend beyond Object Table\n",
			   __func__);
		break;
	    }

	    RHDDebug(handle->scrnIndex, " - Record Type: %x\n",
		     Record->ucRecordType);

	    switch (Record->ucRecordType) {

		case ATOM_I2C_RECORD_TYPE:
		    rhdAtomDDCFromI2CRecord(handle,
					    (ATOM_I2C_RECORD *)Record,
					    &cp[ncon].DDC);
		    break;

		case ATOM_HPD_INT_RECORD_TYPE:
		    rhdAtomHPDFromRecord(handle,
					 (ATOM_HPD_INT_RECORD *)Record,
					 &cp[ncon].HPD);
		    break;

		case ATOM_CONNECTOR_DEVICE_TAG_RECORD_TYPE:
		    taglist = rhdAtomDeviceTagsFromRecord(handle,
							  (ATOM_CONNECTOR_DEVICE_TAG_RECORD *)Record);
		    if (taglist) {
			cp[ncon].Name = RhdAppendString(cp[ncon].Name,taglist);
			xfree(taglist);
		    }
		    break;

		default:
		    break;
	    }

	    Record = (ATOM_COMMON_RECORD_HEADER*)
		((char *)Record + Record->ucRecordSize);

	}

	if ((++ncon) == RHD_CONNECTORS_MAX)
	    break;
    }
    *ptr = cp;

    RhdPrintConnectorInfo(handle->scrnIndex, cp);

    return ATOM_SUCCESS;
}

/*
 *
 */
static AtomBiosResult
rhdAtomConnectorInfoFromSupportedDevices(atomBiosHandlePtr handle,
					 rhdConnectorInfoPtr *ptr)
{
    atomDataTablesPtr atomDataPtr;
    CARD8 crev, frev;
    rhdConnectorInfoPtr cp;
    struct {
	rhdOutputType ot;
	rhdConnectorType con;
	rhdDDC ddc;
	rhdHPD hpd;
	Bool dual;
	char *name;
	char *outputName;
    } devices[ATOM_MAX_SUPPORTED_DEVICE];
    int ncon = 0;
    int n;

    //RHDFUNC(handle);

    atomDataPtr = handle->atomDataPtr;

    if (!rhdAtomGetTableRevisionAndSize(
	    &(atomDataPtr->SupportedDevicesInfo.SupportedDevicesInfo->sHeader),
	    &crev,&frev,NULL)) {
	return ATOM_NOT_IMPLEMENTED;
    }

    if (!(cp = (rhdConnectorInfoPtr)xcalloc(RHD_CONNECTORS_MAX,
					 sizeof(struct rhdConnectorInfo))))
	return ATOM_FAILED;

    for (n = 0; n < ATOM_MAX_SUPPORTED_DEVICE; n++) {
	ATOM_CONNECTOR_INFO_I2C ci
	    = atomDataPtr->SupportedDevicesInfo.SupportedDevicesInfo->asConnInfo[n];

	devices[n].ot = OUTPUT_NONE_ATOM;

	if (!(atomDataPtr->SupportedDevicesInfo
	      .SupportedDevicesInfo->usDeviceSupport & (1 << n)))
	    continue;

	if (Limit(ci.sucConnectorInfo.sbfAccess.bfConnectorType,
		  n_rhd_connectors, "bfConnectorType"))
	    continue;

	devices[n].con
	    = rhd_connectors[ci.sucConnectorInfo.sbfAccess.bfConnectorType].con;
	if (devices[n].con == RHD_CONNECTOR_NONE)
	    continue;

	devices[n].dual
	    = rhd_connectors[ci.sucConnectorInfo.sbfAccess.bfConnectorType].dual;
	devices[n].name
	    = rhd_connectors[ci.sucConnectorInfo.sbfAccess.bfConnectorType].name;

	RHDDebug(handle->scrnIndex,"AtomBIOS Connector[%i]: %s Device:%s ",n,
		 rhd_connectors[ci.sucConnectorInfo
				.sbfAccess.bfConnectorType].name,
		 rhd_devices[n].name);

	devices[n].outputName = rhd_devices[n].name;

	if (!Limit(ci.sucConnectorInfo.sbfAccess.bfAssociatedDAC,
		   n_acc_dac, "bfAssociatedDAC")) {
	    if ((devices[n].ot
		 = acc_dac[ci.sucConnectorInfo.sbfAccess.bfAssociatedDAC])
		== OUTPUT_NONE_ATOM) {
		devices[n].ot = rhd_devices[n].ot;
	    }
	} else
	    devices[n].ot = OUTPUT_NONE_ATOM;

	RHDDebugCont("Output: %x ",devices[n].ot);

	if (ci.sucI2cId.sbfAccess.bfHW_Capable) {

	    RHDDebugCont("HW DDC %i ",
			 ci.sucI2cId.sbfAccess.bfI2C_LineMux);

	    if (Limit(ci.sucI2cId.sbfAccess.bfI2C_LineMux,
		      n_hwddc, "bfI2C_LineMux"))
		devices[n].ddc = RHD_DDC_NONE;
	    else
		devices[n].ddc = hwddc[ci.sucI2cId.sbfAccess.bfI2C_LineMux];

	} else if (ci.sucI2cId.sbfAccess.bfI2C_LineMux) {

	    RHDDebugCont("GPIO DDC ");
	    devices[n].ddc = RHD_DDC_GPIO;

	    /* add support for GPIO line */
	} else {

	    RHDDebugCont("NO DDC ");
	    devices[n].ddc = RHD_DDC_NONE;

	}

	if (crev > 1) {
	    ATOM_CONNECTOR_INC_SRC_BITMAP isb
		= atomDataPtr->SupportedDevicesInfo
		.SupportedDevicesInfo_HD->asIntSrcInfo[n];

	    switch (isb.ucIntSrcBitmap) {
		case 0x4:
		    RHDDebugCont("HPD 0\n");
		    devices[n].hpd = RHD_HPD_0;
		    break;
		case 0xa:
		    RHDDebugCont("HPD 1\n");
		    devices[n].hpd = RHD_HPD_1;
		    break;
		default:
		    RHDDebugCont("NO HPD\n");
		    devices[n].hpd = RHD_HPD_NONE;
		    break;
	    }
	} else {
	    RHDDebugCont("NO HPD\n");
	    devices[n].hpd = RHD_HPD_NONE;
	}
    }
    /* sort devices for connectors */
    for (n = 0; n < ATOM_MAX_SUPPORTED_DEVICE; n++) {
	int i;

	if (devices[n].ot == OUTPUT_NONE_ATOM)
	    continue;
	if (devices[n].con == CONNECTOR_NONE_ATOM)
	    continue;

	cp[ncon].DDC = devices[n].ddc;
	cp[ncon].HPD = devices[n].hpd;
	cp[ncon].Output[0] = devices[n].ot;
	cp[ncon].Output[1] = OUTPUT_NONE_ATOM;
	cp[ncon].Type = devices[n].con;
	cp[ncon].Name = xf86strdup(devices[n].name);
	cp[ncon].Name = RhdAppendString(cp[ncon].Name, devices[n].outputName);

	if (devices[n].dual) {
	    if (devices[n].ddc == RHD_DDC_NONE)
		xf86DrvMsg(handle->scrnIndex, X_ERROR,
			   "No DDC channel for device %s found."
			   " Cannot find matching device.\n",devices[n].name);
	    else {
		for (i = n + 1; i < ATOM_MAX_SUPPORTED_DEVICE; i++) {

		    if (!devices[i].dual)
			continue;

		    if (devices[n].ddc != devices[i].ddc)
			continue;

		    if (((devices[n].ot == OUTPUT_DACA_ATOM
			  || devices[n].ot == OUTPUT_DACB_ATOM)
			 && (devices[i].ot == OUTPUT_LVTMA_ATOM
			     || devices[i].ot == OUTPUT_TMDSA_ATOM))
			|| ((devices[i].ot == OUTPUT_DACA_ATOM
			     || devices[i].ot == OUTPUT_DACB_ATOM)
			    && (devices[n].ot == OUTPUT_LVTMA_ATOM
				|| devices[n].ot == OUTPUT_TMDSA_ATOM))) {

			cp[ncon].Output[1] = devices[i].ot;

			if (cp[ncon].HPD == RHD_HPD_NONE)
			    cp[ncon].HPD = devices[i].hpd;

			cp[ncon].Name = RhdAppendString(cp[ncon].Name,
							devices[i].outputName);
			devices[i].ot = OUTPUT_NONE_ATOM; /* zero the device */
		    }
		}
	    }
	}

	if ((++ncon) == RHD_CONNECTORS_MAX)
	    break;
    }
    *ptr = cp;

    RhdPrintConnectorInfo(handle->scrnIndex, cp);

    return ATOM_SUCCESS;
}

/*
 *
 */
static AtomBiosResult
rhdAtomConnectorInfo(atomBiosHandlePtr handle,
		     AtomBiosRequestID unused, AtomBiosArgPtr data)
{
    data->connectorInfo = NULL;

    if (rhdAtomConnectorInfoFromObjectHeader(handle,&data->connectorInfo)
	== ATOM_SUCCESS)
	return ATOM_SUCCESS;
    else
	return rhdAtomConnectorInfoFromSupportedDevices(handle,
							&data->connectorInfo);
}
#endif

# ifdef ATOM_BIOS_PARSER
static AtomBiosResult
rhdAtomExec (atomBiosHandlePtr handle,
	     AtomBiosRequestID unused, AtomBiosArgPtr data)
{
    RADEONInfoPtr info = RADEONPTR (xf86Screens[handle->scrnIndex]);
    Bool ret = FALSE;
    char *msg;
    int idx = data->exec.index;
    void *pspace = data->exec.pspace;
    pointer *dataSpace = data->exec.dataSpace;

    //RHDFUNCI(handle->scrnIndex);

    if (dataSpace) {
	if (!handle->fbBase && !handle->scratchBase)
	    return ATOM_FAILED;
	if (handle->fbBase) {
	    if (!info->FB) {
		xf86DrvMsg(handle->scrnIndex, X_ERROR, "%s: "
			   "Cannot exec AtomBIOS: framebuffer not mapped\n",
			   __func__);
		return ATOM_FAILED;
	    }
	    *dataSpace = (CARD8*)info->FB + handle->fbBase;
	} else
	    *dataSpace = (CARD8*)handle->scratchBase;
    }
    ret = ParseTableWrapper(pspace, idx, handle,
			    handle->BIOSBase,
			    &msg);
    if (!ret)
	xf86DrvMsg(handle->scrnIndex, X_ERROR, "%s\n",msg);
    else
	xf86DrvMsgVerb(handle->scrnIndex, X_INFO, 5, "%s\n",msg);

    return (ret) ? ATOM_SUCCESS : ATOM_FAILED;
}
# endif

AtomBiosResult
RHDAtomBiosFunc(int scrnIndex, atomBiosHandlePtr handle,
		AtomBiosRequestID id, AtomBiosArgPtr data)
{
    AtomBiosResult ret = ATOM_FAILED;
    int i;
    char *msg = NULL;
    enum msgDataFormat msg_f = MSG_FORMAT_NONE;
    AtomBiosRequestFunc req_func = NULL;

    //RHDFUNCI(scrnIndex);

    for (i = 0; AtomBiosRequestList[i].id != FUNC_END; i++) {
	if (id ==  AtomBiosRequestList[i].id) {
	    req_func = AtomBiosRequestList[i].request;
	    msg = AtomBiosRequestList[i].message;
	    msg_f = AtomBiosRequestList[i].message_format;
	    break;
	}
    }

    if (req_func == NULL) {
	xf86DrvMsg(scrnIndex, X_ERROR, "Unknown AtomBIOS request: %i\n",id);
	return ATOM_NOT_IMPLEMENTED;
    }
    /* Hack for now */
    if (id == ATOMBIOS_INIT)
	data->val = scrnIndex;

    if (id == ATOMBIOS_INIT || handle)
	ret = req_func(handle, id, data);

    if (ret == ATOM_SUCCESS) {

	switch (msg_f) {
	    case MSG_FORMAT_DEC:
		xf86DrvMsg(scrnIndex,X_INFO,"%s: %li\n", msg,
			   (unsigned long) data->val);
		break;
	    case MSG_FORMAT_HEX:
		xf86DrvMsg(scrnIndex,X_INFO,"%s: 0x%lx\n",msg ,
			   (unsigned long) data->val);
		break;
	    case MSG_FORMAT_NONE:
		xf86DrvMsgVerb(scrnIndex, 7, X_INFO,
			       "Call to %s succeeded\n", msg);
		break;
	}

    } else {

	char *result = (ret == ATOM_FAILED) ? "failed"
	    : "not implemented";
	switch (msg_f) {
	    case MSG_FORMAT_DEC:
	    case MSG_FORMAT_HEX:
		xf86DrvMsgVerb(scrnIndex, 1, X_WARNING,
			       "Call to %s %s\n", msg, result);
		break;
	    case MSG_FORMAT_NONE:
		xf86DrvMsg(scrnIndex,X_INFO,"Query for %s: %s\n", msg, result);
		    break;
	}
    }
    return ret;
}

# ifdef ATOM_BIOS_PARSER
VOID*
CailAllocateMemory(VOID *CAIL,UINT16 size)
{
    CAILFUNC(CAIL);

    return malloc(size);
}

VOID
CailReleaseMemory(VOID *CAIL, VOID *addr)
{
    CAILFUNC(CAIL);

    free(addr);
}

VOID
CailDelayMicroSeconds(VOID *CAIL, UINT32 delay)
{
    CAILFUNC(CAIL);

    usleep(delay);

    DEBUGP(xf86DrvMsg(((atomBiosHandlePtr)CAIL)->scrnIndex,X_INFO,"Delay %i usec\n",delay));
}

UINT32
CailReadATIRegister(VOID* CAIL, UINT32 idx)
{
    ScrnInfoPtr pScrn = xf86Screens[((atomBiosHandlePtr)CAIL)->scrnIndex];
    RADEONInfoPtr  info   = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    UINT32 ret;
    CAILFUNC(CAIL);

    ret  =  INREG(idx << 2);
    DEBUGP(ErrorF("%s(%x) = %x\n",__func__,idx << 2,ret));
    return ret;
}

VOID
CailWriteATIRegister(VOID *CAIL, UINT32 idx, UINT32 data)
{
    ScrnInfoPtr pScrn = xf86Screens[((atomBiosHandlePtr)CAIL)->scrnIndex];
    RADEONInfoPtr  info   = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    CAILFUNC(CAIL);

    OUTREG(idx << 2,data);
    DEBUGP(ErrorF("%s(%x,%x)\n",__func__,idx << 2,data));
}

UINT32
CailReadFBData(VOID* CAIL, UINT32 idx)
{
    ScrnInfoPtr pScrn = xf86Screens[((atomBiosHandlePtr)CAIL)->scrnIndex];
    RADEONInfoPtr  info   = RADEONPTR(pScrn);
    UINT32 ret;

    CAILFUNC(CAIL);

    if (((atomBiosHandlePtr)CAIL)->fbBase) {
	CARD8 *FBBase = (CARD8*)info->FB;
	ret =  *((CARD32*)(FBBase + (((atomBiosHandlePtr)CAIL)->fbBase) + idx));
	DEBUGP(ErrorF("%s(%x) = %x\n",__func__,idx,ret));
    } else if (((atomBiosHandlePtr)CAIL)->scratchBase) {
	ret = *(CARD32*)((CARD8*)(((atomBiosHandlePtr)CAIL)->scratchBase) + idx);
	DEBUGP(ErrorF("%s(%x) = %x\n",__func__,idx,ret));
    } else {
	xf86DrvMsg(((atomBiosHandlePtr)CAIL)->scrnIndex,X_ERROR,
		   "%s: no fbbase set\n",__func__);
	return 0;
    }
    return ret;
}

VOID
CailWriteFBData(VOID *CAIL, UINT32 idx, UINT32 data)
{
    CAILFUNC(CAIL);

    DEBUGP(ErrorF("%s(%x,%x)\n",__func__,idx,data));
    if (((atomBiosHandlePtr)CAIL)->fbBase) {
	CARD8 *FBBase = (CARD8*)
	    RADEONPTR(xf86Screens[((atomBiosHandlePtr)CAIL)->scrnIndex])->FB;
	*((CARD32*)(FBBase + (((atomBiosHandlePtr)CAIL)->fbBase) + idx)) = data;
    } else if (((atomBiosHandlePtr)CAIL)->scratchBase) {
	*(CARD32*)((CARD8*)(((atomBiosHandlePtr)CAIL)->scratchBase) + idx) = data;
    } else
	xf86DrvMsg(((atomBiosHandlePtr)CAIL)->scrnIndex,X_ERROR,
		   "%s: no fbbase set\n",__func__);
}

ULONG
CailReadMC(VOID *CAIL, ULONG Address)
{
    ScrnInfoPtr pScrn = xf86Screens[((atomBiosHandlePtr)CAIL)->scrnIndex];
    ULONG ret;

    CAILFUNC(CAIL);

    ret = INMC(pScrn, Address);
    DEBUGP(ErrorF("%s(%x) = %x\n",__func__,Address,ret));
    return ret;
}

VOID
CailWriteMC(VOID *CAIL, ULONG Address, ULONG data)
{
    ScrnInfoPtr pScrn = xf86Screens[((atomBiosHandlePtr)CAIL)->scrnIndex];

    CAILFUNC(CAIL);
    DEBUGP(ErrorF("%s(%x,%x)\n",__func__,Address,data));
    OUTMC(pScrn, Address, data);
}

#ifdef XSERVER_LIBPCIACCESS

VOID
CailReadPCIConfigData(VOID*CAIL, VOID* ret, UINT32 idx,UINT16 size)
{
    pci_device_cfg_read(RADEONPTR(xf86Screens[((atomBiosHandlePtr)CAIL)->scrnIndex])->PciInfo,
				ret,idx << 2 , size >> 3, NULL);
}

VOID
CailWritePCIConfigData(VOID*CAIL,VOID*src,UINT32 idx,UINT16 size)
{
    pci_device_cfg_write(RADEONPTR(xf86Screens[((atomBiosHandlePtr)CAIL)->scrnIndex])->PciInfo,
			 src, idx << 2, size >> 3, NULL);
}

#else

VOID
CailReadPCIConfigData(VOID*CAIL, VOID* ret, UINT32 idx,UINT16 size)
{
    PCITAG tag = ((atomBiosHandlePtr)CAIL)->PciTag;

    CAILFUNC(CAIL);

    switch (size) {
	case 8:
	    *(CARD8*)ret = pciReadByte(tag,idx << 2);
	    break;
	case 16:
	    *(CARD16*)ret = pciReadWord(tag,idx << 2);
	    break;
	case 32:
	    *(CARD32*)ret = pciReadLong(tag,idx << 2);
	    break;
	default:
	xf86DrvMsg(((atomBiosHandlePtr)CAIL)->scrnIndex,
		   X_ERROR,"%s: Unsupported size: %i\n",
		   __func__,(int)size);
	return;
	    break;
    }
    DEBUGP(ErrorF("%s(%x) = %x\n",__func__,idx,*(unsigned int*)ret));

}

VOID
CailWritePCIConfigData(VOID*CAIL,VOID*src,UINT32 idx,UINT16 size)
{
    PCITAG tag = ((atomBiosHandlePtr)CAIL)->PciTag;

    CAILFUNC(CAIL);
    DEBUGP(ErrorF("%s(%x,%x)\n",__func__,idx,(*(unsigned int*)src)));
    switch (size) {
	case 8:
	    pciWriteByte(tag,idx << 2,*(CARD8*)src);
	    break;
	case 16:
	    pciWriteWord(tag,idx << 2,*(CARD16*)src);
	    break;
	case 32:
	    pciWriteLong(tag,idx << 2,*(CARD32*)src);
	    break;
	default:
	    xf86DrvMsg(((atomBiosHandlePtr)CAIL)->scrnIndex,X_ERROR,
		       "%s: Unsupported size: %i\n",__func__,(int)size);
	    break;
    }
}
#endif

ULONG
CailReadPLL(VOID *CAIL, ULONG Address)
{
    ScrnInfoPtr pScrn = xf86Screens[((atomBiosHandlePtr)CAIL)->scrnIndex];
    ULONG ret;

    CAILFUNC(CAIL);

    ret = RADEONINPLL(pScrn, Address);
    DEBUGP(ErrorF("%s(%x) = %x\n",__func__,Address,ret));
    return ret;
}

VOID
CailWritePLL(VOID *CAIL, ULONG Address,ULONG Data)
{
    ScrnInfoPtr pScrn = xf86Screens[((atomBiosHandlePtr)CAIL)->scrnIndex];
    CAILFUNC(CAIL);

    DEBUGP(ErrorF("%s(%x,%x)\n",__func__,Address,Data));
    RADEONOUTPLL(pScrn, Address, Data);
}

void
atombios_get_command_table_version(atomBiosHandlePtr atomBIOS, int index, int *major, int *minor)
{
    ATOM_MASTER_COMMAND_TABLE *cmd_table = atomBIOS->BIOSBase + atomBIOS->cmd_offset;
    ATOM_MASTER_LIST_OF_COMMAND_TABLES *table_start;
    ATOM_COMMON_ROM_COMMAND_TABLE_HEADER *table_hdr;

    //unsigned short *ptr;
    unsigned short offset;

    table_start = &cmd_table->ListOfCommandTables;

    offset  = *(((unsigned short *)table_start) + index);

    table_hdr = atomBIOS->BIOSBase + offset;

    *major = table_hdr->CommonHeader.ucTableFormatRevision;
    *minor = table_hdr->CommonHeader.ucTableContentRevision;
}


#endif /* ATOM_BIOS */
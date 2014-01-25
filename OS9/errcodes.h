/* error code conditions */
/* WDDisk "TEST" Error Codes */
enum ErrCode {
  E_WD2797 = 101,		/* WD 1002-05 WD2797 FD controller */
  E_WD1010,			/* WD 1002-05 WD1010 HD controller */
  E_WDSBuf,			/* WD 1002-05 sector buffer */
  E_WD1014,			/* WD 1002-05 WD1014 error detection or bus */
  E_WD1015,			/* WD 1002-05 WD1015 control processor */

  /* System Dependent Error Codes */
  E_IWTyp = 183,		/* illegal window type */
  E_WADef,			/* window already defined */
  E_NFont,			/* font not found */
  E_StkOvf,			/* Stack overflow */
  E_IllArg,			/* Illegal argument */
  E_188,			/* reserved */
  E_ICoord,			/* Illegal coordinates */
  E_Bug,			/* Bug (should never be returned) */
  E_BufSiz,			/* Buffer size is too small */
  E_IllCmd,			/* Illegal command */
  E_TblFul,			/* Screen or window table is full */
  E_BadBuf,			/* Bad/Undefined buffer number */
  E_IWDef,			/* Illegal window definition */
  E_WUndef,			/* Window undefined */
  E_Up,				/* up-arrow pressed on SCF I$ReadLn with PD.UP enabled */
  E_Dn,				/* dn-arrow pressed on SCF I$ReadLn with PD.DN enabled */
  E_Alias, 

  /* Standard OS-9 Error Codes */
  E_PthFul = 200,		/* Path Table full */
  E_BPNum,			/* Bad Path Number */
  E_Poll,			/* Polling Table Full */
  E_BMode,			/* Bad Mode */
  E_DevOvf,			/* Device Table Overflow */
  E_BMID,			/* Bad Module ID */
  E_DirFul,			/* Module Directory Full */
  E_MemFul,			/* Process Memory Full */
  
  E_UnkSvc,			/* Unknown Service Code */
  E_ModBsy,			/* Module Busy */
  E_BPAddr,			/* Bad Page Address */
  E_EOF,			/* End of File */
  E_xxx1, 
  E_NES,			/* Non-Existing Segment */
  E_FNA,			/* File Not Accesible */
  E_BPNam,			/* Bad Path Name */
  E_PNNF,			/* Path Name Not Found */
  E_SLF,			/* Segment List Full */
  E_CEF,			/* Creating Existing File */
  E_IBA,			/* Illegal Block Address */
  E_HangUp,			/* Carrier Detect Lost */
  E_MNF,			/* Module Not Found */
  E_xxx2, 
  E_DelSP,			/* Deleting Stack Pointer memory */
  E_IPrcID,			/* Illegal Process ID */
#define E_BPrcID E_IPrcID	/* Bad Process ID (formerly #238) */
  E_xxx3, 
  E_NoChld,			/* No Children */
  E_ISWI,			/* Illegal SWI code */
  E_PrcAbt,			/* Process Aborted */
  E_PrcFul,			/* Process Table Full */
  E_IForkP,			/* Illegal Fork Parameter */
  E_KwnMod,			/* Known Module */
  E_BMCRC,			/* Bad Module CRC */
  E_USigP,			/* Unprocessed Signal Pending */
  E_NEMod,			/* Non Existing Module */
  E_BNam,			/* Bad Name */
  E_BMHP,			/* (bad module header parity) */
  E_NoRam,			/* No (System) Ram Available */
  E_DNE,			/* Directory not empty */
  E_NoTask,			/* No available Task number */

  E_Unit = 240,			/* Illegal Unit (drive) */
  E_Sect,			/* Bad SECTor number */
  E_WP,				/* Write Protect */
  E_CRC,			/* Bad Check Sum */
  E_Read,			/* Read Error */
  E_Write,			/* Write Error */
  E_NotRdy,			/* Device Not Ready */
  E_Seek,			/* Seek Error */
  E_Full,			/* Media Full */
  E_BTyp,			/* Bad Type (incompatable) media */
  E_DevBsy,			/* Device Busy */
  E_DIDC,			/* Disk ID Change */
  E_Lock,			/* Record is busy (locked out) */
  E_Share,			/* Non-sharable file busy */
  E_DeadLk,			/* I/O Deadlock error */
};


/********************************************************************************/
/*    H   H Y   Y TTTTT EEEEE  CCC       HYTEC ELECTRONICS LTD                  */
/*    H   H  Y Y    T   E     C          5 Cradock Road,                        */
/*    HHHHH   Y     T   EEE   C          Reading, Berks.    Tel: 0118 9757770   */
/*    H   H   Y     T   E     C          RG2 0JT            Fax: 0118 9757566   */
/*    H   H   Y     T   EEEEE  CCC       Web: www.hytec-electronics.co.uk       */
/********************************************************************************/
/*                                                                              */
/* linux-driver.h - Header for Hytec Linux driver for IOC 9010.                 */
/*                                                                              */
/* (C)2006 Hytec Electronics Ltd.                                               */
/*                                                                              */
/********************************************************************************/
/*                                                                              */
/*  Revision history: (comment and initial revisions)                           */
/*                                                                              */
/*  vers.       revised         modified by                                     */
/*  -----       -----------     -------------------------                       */
/*  1.1			29-SEP-2005		D. Nineham, Hytec Electronics Ltd.				*/
/*                              Initial version.                                */
/*                                                                              */
/*  1.2			05-SEP-2006		D. Nineham, Hytec Electronics Ltd.				*/
/*                              1. Delay Loops replaced.                        */
/*                                                                              */
/*  1.3			09-OCT-2006		D. Nineham, Hytec Electronics Ltd.				*/
/*                              1. Support for 5331 Added.                      */
/*                                                                              */
/*  1.4			20-SEP-2007		D. Nineham, Hytec Electronics Ltd.				*/
/*                              1.                                              */
/*                                                                              */
/*  1.5			08-JUL-2008		D. Nineham / M. Woodward, Hytec Electronics Ltd.*/
/*                              1. Added 32bit read / write IOCTL functions     */
/*                                                                              */
/*  1.6			11-NOV-2008		M. Woodward, Hytec Electronics Ltd.				*/
/*                              1. Added support for PCI 5332 card				*/
/*                                                                              */
/*  1.7			14-JAN-2008		M. Woodward, Hytec Electronics Ltd.				*/
/*                              1. Fixed problem with 16bit memory writes		*/
/*                                 overwriting adjacent mem locations			*/
/*																				*/
/*  1.8			26-MAR-2009		J. Chen, Hytec Electronics Ltd.					*/
/*                              1. Fixed driver reload problem (region not 		*/
/*                                 released up on close)						*/
/*								2. Added interrupt support for 5331/3331		*/
/*										THE INTERRUPT MECHANISM					*/
/*								3. Added release function up on object destroy.	*/
/*								   This is used for unregistering interrupt		*/
/*								   handlers for those who want interrupt 		*/
/*								   support.										*/
/*								4. Added fasync function for process 			*/
/*								   registration who interested in interrupt		*/
/*								5. Modified read function to return interrupt	*/
/*								   vector to the user. So this read doesn't		*/
/*								   do anything else								*/
/*								6. Sorted out the interrupt support for 9010	*/
/*								7. Added support for Hytec 6335 PCI Express 	*/
/*								   card											*/
/*  1.9			19-MAY-2009		J. Chen, Hytec Electronics Ltd.					*/
/*								1. Changed interrupt level fetch by using 		*/
/*								   pci_get_dev call (old fashion but works)		*/
/*								2. Changed removing resource only when memory 	*/
/*								   mapping and irq assigning are successful		*/
/*  					        3. Commented out kprint in open and release		*/
/*  2.0			02-JUN-2009		J. Chen, Hytec Electronics Ltd.					*/
/*  					        Use a macro define to determine kprint     		*/
/*  3.0			17-SEP-2009		J. Chen, Hytec Electronics Ltd.					*/
/*  					        Added memory mapping from physical address to 	*/
/*  					        the user space 	                                */
/*  3.1			28-SEP-2009		J. Chen, Hytec Electronics Ltd.					*/
/*  					        Added fast interrupt call back to the user      */
/*  					        space via block read function of the driver     */
/*  3.2			19-JAN-2010		J. Chen, Hytec Electronics Ltd.					*/
/*  					        Added one ioctl call for returning "Base ADD3"  */
/*  					        for user space memory mapping                   */
/*  3.3			14-JUL-2010		J. Chen, Hytec Electronics Ltd.					*/
/*  					        Minor change for version control  				*/
/*  3.4			15-JUL-2010		J. Chen, Hytec Electronics Ltd.					*/
/*  					        Commented out the the version detection			*/
/*  3.5			15-JUL-2010		J. Chen, Hytec Electronics Ltd.					*/
/*  					        Added support for Hytec uTCA 7002 card			*/
/*  3.6			11-OCT-2010		J. Chen, Hytec Electronics Ltd.					*/
/*  					        Added interrupt support for Hytec uTCA 7002 card*/
/*  3.7			11-OCT-2010		K. Jogia, Hytec Electronics Ltd.				*/
/*  					        Added BAR 2 & 1 in IOCTL, & Disable interrupts	*/
/*  4.0			19-NOV-2010		J. Chen, K. Jogia, Hytec Electronics Ltd.		*/
/*  					        Converted the driver for supporting multiple 	*/
/*								devices											*/
/*  4.1			26-NOV-2010		J. Chen, Hytec Electronics Ltd.					*/
/*  					        Changed Read function to return both interrupt 	*/
/*								vector and carrier id for multiple carrier case.*/
/*  4.2			30-NOV-2010		J. Chen, Hytec Electronics Ltd.					*/
/*  					        Modified read function to handle multiple 		*/
/*								carrier interrupts.								*/
/*  4.3			01-DEC-2010		J. Chen, Hytec Electronics Ltd.					*/
/*  					        Modified the interrupt service routine to pass	*/
/*								some spurious(?) (IOC9010) interrupts over		*/
/*								Apperently this is caused by the error interrupt*/
/*								mask. Now these mask bits have been cleared. 	*/
/*  4.4			06-DEC-2010		J. Chen, Hytec Electronics Ltd.					*/
/*  					        Added clean up on error for device registration.*/
/*								Force the driver to use dynamic numbering to	*/
/*								complaint to the main stream.					*/
/*  4.5			14-FEB-2011		J. Chen, Hytec Electronics Ltd.					*/
/*								Changed error wording in io/mem region requests */
/*								to indicate other driver is also uses them		*/
/*																				*/
/*								Modified interrupt service routine to return	*/
/*								IRQ_NONE if the interrupt is not for this 		*/
/*								driver. But if it is and not interrupt source	*/
/*								found, it is a spurious interrupt then return	*/
/*								IRQ_HANDLED to kill it							*/
/*																				*/
/*								Added Hytec 8515/8516 card detection to check 	*/
/*								whether these devices have been used by UART	*/
/*								driver. It they are, the interrupt service		*/
/*								routine in this driver wouldn't handle serail 	*/
/*								related interrupts and leave them to the UART.	*/
/*  5.0			14-MAR-2011		J. Chen, Hytec Electronics Ltd.					*/
/*								Major changes for handling interrupts. Now the  */
/*								driver can handle multiple devices. Each device	*/
/*								can have multiple files. And each file can have */
/*								multiple IP cards (vectors). The ISR can 		*/
/*								dispatch the interrupt vector to the correct	*/
/*								IP card user end callback function.				*/
/*																				*/
/********************************************************************************/


/*******************************************************************************
*
* COMPILATION OPTIONS
*
*   __DEBUG__
*       Define this if you want diagnostics output from the driver as it runs.
*       While very useful for debugging (hence the name), this takes up a lot of
*       CPU when running live.
*
*   __DIRECT_IO__
*       Define this if you want to include the direct I/O routines in the
*       driver. These routines access the FIFO directly and are no longer for
*       general use.
*
*******************************************************************************/

#include <linux/ioctl.h>

/*---- definitions -------------------------------------------------*/


/* Register Offsets from Base */
#define REG_CSR					0x0000
#define REG_SWITCH				0x0002
#define REG_LCD_CONTROL			0x0004
#define REG_LCD_DATA			0x0006
#define REG_IP_INT				0x0008
#define REG_IP_ERROR_INT		0x000A
#define REG_IP_INT_MASK			0x000C
#define REG_IP_ERROR_MASK		0x000E
#define REG_IP_CLOCK			0x0010

#define REG_FANS_1_2			0x0012
#define REG_FANS_3_4			0x0014
#define REG_FANS_5_6			0x0016
#define REG_FANS_CONTROL		0x0018
#define REG_TEMPERATURES		0x001A
#define REG_CONFIG_2			0x001C


/* CSR Register Bit Map */
#define CSR_PMC_PRESENT			0x0001
#define CSR_TIMEOUT_INT_CLEAR	0x0002
#define CSR_SPARE_2				0x0004
#define CSR_FAN_1_OK			0x0008
#define CSR_FAN_2_OK			0x0010
#define CSR_FAN_3_OK			0x0020
#define CSR_FAN_4_OK			0x0040
#define CSR_FAN_5_OK			0x0080
#define CSR_INPUT_1				0x0100
#define CSR_INPUT_2				0x0200
#define CSR_INPUT_3				0x0400
#define CSR_SPARE_3				0x0800
#define CSR_SPARE_4				0x1000
#define CSR_SPARE_5				0x2000
#define CSR_SPARE_6				0x4000
#define CSR_SPARE_7				0x8000


/* SWITCH Register Bit Map */
#define SWITCH_UP				0x0001
#define SWITCH_OK				0x0002
#define SWITCH_DOWN				0x0004
#define SWITCH_RESET			0x0008
#define SWITCH_SPARE1			0x0010
#define SWITCH_SPARE2			0x0020
#define SWITCH_SPARE3			0x0040
#define SWITCH_SPARE4			0x0080
#define SWITCH_SW0				0x0100
#define SWITCH_SW1				0x0200
#define SWITCH_SW2				0x0400
#define SWITCH_SW3				0x0800
#define SWITCH_SW4				0x1000
#define SWITCH_SW5				0x2000
#define SWITCH_SW6				0x4000
#define SWITCH_SW7				0x8000


/* LCD Control Register Bit Map */
#define LCD_CONTROL_0			0x0001
#define LCD_CONTROL_1			0x0002
#define LCD_CONTROL_2			0x0004
#define LCD_CONTROL_3			0x0008
#define LCD_CONTROL_4			0x0010
#define LCD_CONTROL_5			0x0020
#define LCD_CONTROL_6			0x0040
#define LCD_CONTROL_7			0x0080
#define LCD_CONTROL_8			0x0100
#define LCD_CONTROL_9			0x0200
#define LCD_CONTROL_10			0x0400
#define LCD_CONTROL_11			0x0800
#define LCD_CONTROL_12			0x1000
#define LCD_CONTROL_13			0x2000
#define LCD_CONTROL_14			0x4000
#define LCD_CONTROL_15			0x8000


/* LCD Data Register Bit Map */
#define LCD_DATA_0				0x0001
#define LCD_DATA_1				0x0002
#define LCD_DATA_2				0x0004
#define LCD_DATA_3				0x0008
#define LCD_DATA_4				0x0010
#define LCD_DATA_5				0x0020
#define LCD_DATA_6				0x0040
#define LCD_DATA_7				0x0080
#define LCD_DATA_8				0x0100
#define LCD_DATA_9				0x0200
#define LCD_DATA_10				0x0400
#define LCD_DATA_11				0x0800
#define LCD_DATA_12				0x1000
#define LCD_DATA_13				0x2000
#define LCD_DATA_14				0x4000
#define LCD_DATA_15				0x8000


/* IP Interrupt Register Bit Map */
#define IP_INT_A_IRQ0			0x0001
#define IP_INT_A_IRQ1			0x0002
#define IP_INT_B_IRQ0			0x0004
#define IP_INT_B_IRQ1			0x0008
#define IP_INT_C_IRQ0			0x0010
#define IP_INT_C_IRQ1			0x0020
#define IP_INT_D_IRQ0			0x0040
#define IP_INT_D_IRQ1			0x0080
#define IP_INT_E_IRQ0			0x0100
#define IP_INT_E_IRQ1			0x0200
#define IP_INT_F_IRQ0			0x0400
#define IP_INT_F_IRQ1			0x0800
#define IP_INT_SPARE1			0x1000
#define IP_INT_SPARE2			0x2000
#define IP_INT_SPARE3			0x4000
#define IP_INT_SPARE4			0x8000


/* IP Error Interrupt Register Bit Map */
#define IP_ERROR_INT_A			0x0001
#define IP_ERROR_INT_B			0x0002
#define IP_ERROR_INT_C			0x0004
#define IP_ERROR_INT_D			0x0008
#define IP_ERROR_INT_E			0x0010
#define IP_ERROR_INT_F			0x0020
#define IP_ERROR_SPARE0			0x0040
#define IP_ERROR_SPARE1			0x0080
#define IP_ERROR_SPARE2			0x0100
#define IP_ERROR_SPARE3			0x0200
#define IP_ERROR_SPARE4			0x0400
#define IP_ERROR_SPARE5			0x0800
#define IP_ERROR_SPARE6			0x1000
#define IP_ERROR_SPARE7			0x2000
#define IP_ERROR_SPARE8			0x4000
#define IP_ERROR_SPARE9			0x8000



/* IP Interrupt Mask Register Bit Map */
#define IP_INT_MASK_A_IRQ0		0x0001
#define IP_INT_MASK_A_IRQ1		0x0002
#define IP_INT_MASK_B_IRQ0		0x0004
#define IP_INT_MASK_B_IRQ1		0x0008
#define IP_INT_MASK_C_IRQ0		0x0010
#define IP_INT_MASK_C_IRQ1		0x0020
#define IP_INT_MASK_D_IRQ0		0x0040
#define IP_INT_MASK_D_IRQ1		0x0080
#define IP_INT_MASK_E_IRQ0		0x0100
#define IP_INT_MASK_E_IRQ1		0x0200
#define IP_INT_MASK_F_IRQ0		0x0400
#define IP_INT_MASK_F_IRQ1		0x0800
#define IP_INT_MASK_SPARE1		0x1000
#define IP_INT_MASK_SPARE2		0x2000
#define IP_INT_MASK_SPARE3		0x4000
#define IP_INT_MASK_SPARE4		0x8000


/* IP Error Interrupt Mask Register Bit Map */
#define IP_ERROR_MASK_INT_A		0x0001
#define IP_ERROR_MASK_INT_B		0x0002
#define IP_ERROR_MASK_INT_C		0x0004
#define IP_ERROR_MASK_INT_D		0x0008
#define IP_ERROR_MASK_INT_E		0x0010
#define IP_ERROR_MASK_INT_F		0x0020
#define IP_ERROR_MASK_SPARE0	0x0040
#define IP_ERROR_MASK_SPARE1	0x0080
#define IP_ERROR_MASK_SPARE2	0x0100
#define IP_ERROR_MASK_SPARE3	0x0200
#define IP_ERROR_MASK_SPARE4	0x0400
#define IP_ERROR_MASK_SPARE5	0x0800
#define IP_ERROR_MASK_SPARE6	0x1000
#define IP_ERROR_MASK_SPARE7	0x2000
#define IP_ERROR_MASK_SPARE8	0x4000
#define IP_ERROR_MASK_SPARE9    0x8000

/* for interrupt support. Added by Jim on 26-MAR-2009 */
#define TASK_MAXPID             0x0040      //can notify maximum 64 processes

/* IP Clock Register Bit Map */
#define CLK_IP_A_8MHZ			0x0000
#define CLK_IP_A_32MHZ			0x0001
#define CLK_IP_B_8MHZ			0x0000
#define CLK_IP_B_32MHZ			0x0002
#define CLK_IP_C_8MHZ			0x0000
#define CLK_IP_C_32MHZ			0x0004
#define CLK_IP_D_8MHZ			0x0000
#define CLK_IP_D_32MHZ			0x0008
#define CLK_IP_E_8MHZ			0x0000
#define CLK_IP_E_32MHZ			0x0010
#define CLK_IP_F_8MHZ			0x0000
#define CLK_IP_F_32MHZ			0x0020
#define CLK_IP_SPARE1			0x0040
#define CLK_IP_SPARE2			0x0080
#define CLK_IP_SPARE3			0x0100
#define CLK_IP_SPARE4			0x0200
#define CLK_IP_SPARE5			0x0400
#define CLK_IP_SPARE6			0x0800
#define CLK_IP_SPARE7			0x1000
#define CLK_IP_SPARE8			0x2000
#define CLK_IP_SPARE9			0x4000
#define CLK_IP_SPARE10			0x8000


/* 9010 Maps all access to the IP Cards as Memory starting at Base Address 3 */
#define IP_A_MEMORY_BASE_ADDR	0x000000
#define IP_B_MEMORY_BASE_ADDR	0x200000
#define IP_C_MEMORY_BASE_ADDR	0x400000
#define IP_D_MEMORY_BASE_ADDR	0x600000
#define IP_E_MEMORY_BASE_ADDR	0x800000
#define IP_F_MEMORY_BASE_ADDR	0xA00000
#define IP_IO_BASE_ADDR			0xE00000


/* The IP I/O Address Space is sub-divided by the following offsets...*/
#define IP_A_IO_BASE_ADDR		0x000
#define IP_A_ID_BASE_ADDR		0x080
#define IP_B_IO_BASE_ADDR		0x100
#define IP_B_ID_BASE_ADDR		0x180
#define IP_C_IO_BASE_ADDR		0x200
#define IP_C_ID_BASE_ADDR		0x280
#define IP_D_IO_BASE_ADDR		0x300
#define IP_D_ID_BASE_ADDR		0x380
#define IP_E_IO_BASE_ADDR		0x400
#define IP_E_ID_BASE_ADDR		0x480
#define IP_F_IO_BASE_ADDR		0x500
#define IP_F_ID_BASE_ADDR		0x580
#define IP_A_INT_BASE_ADDR		0x800
#define IP_B_INT_BASE_ADDR		0x900
#define IP_C_INT_BASE_ADDR		0xA00
#define IP_D_INT_BASE_ADDR		0xB00
#define IP_E_INT_BASE_ADDR		0xC00
#define IP_F_INT_BASE_ADDR		0xD00

/* The IP Card's ID PROM Follows the 'VITA4 ' Standard */
/* The Below Defines are the offset from each IP Cards */
#define ID_ASCII_VI				0
#define ID_ASCII_TA				2
#define ID_ASCII_4				4
#define ID_HYTEC_ID_HIGH		6		
#define ID_HYTEC_ID_LOW			8
#define ID_MODEL_NUMBER			10
#define ID_REVISION				12		

/* IOCTL operation codes */

#define HYTEC_IOC_MAGIC             'H'

#define OP_GET_CONFIG	            _IO( HYTEC_IOC_MAGIC, 1)
#define OP_GENERAL_READ				_IOR(HYTEC_IOC_MAGIC, 2 , int)
#define OP_GENERAL_READ_BLOCK		_IOR(HYTEC_IOC_MAGIC, 3 , int)
#define OP_GENERAL_WRITE_BLOCK		_IOW(HYTEC_IOC_MAGIC, 4 , int)
#define OP_CARRIER_READ_BLOCK		_IOR(HYTEC_IOC_MAGIC, 5 , int)
#define OP_CARRIER_WRITE_BLOCK		_IOW(HYTEC_IOC_MAGIC, 6 , int)
#define OP_GENERAL_MEM_READ_BLOCK	_IOR(HYTEC_IOC_MAGIC, 7 , int)
#define OP_GENERAL_MEM_WRITE_BLOCK	_IOW(HYTEC_IOC_MAGIC, 8 , int)
#define OP_GENERAL_VME_READ_BLOCK	_IOR(HYTEC_IOC_MAGIC, 9 , int)
#define OP_GENERAL_VME_WRITE_BLOCK	_IOW(HYTEC_IOC_MAGIC, 10, int)
#define OP_ENABLE_INTERRUPT         _IO( HYTEC_IOC_MAGIC, 11)
#define OP_GENERAL_VME_READ_BLOCK_32    _IOR(HYTEC_IOC_MAGIC, 12, int)
#define OP_GENERAL_VME_WRITE_BLOCK_32   _IOW(HYTEC_IOC_MAGIC, 13, int)
#define OP_GET_5331_CSR	            _IO( HYTEC_IOC_MAGIC, 14)
#define OP_GET_3331_CSR	            _IO( HYTEC_IOC_MAGIC, 14)
#define OP_3331_RESET	            _IO( HYTEC_IOC_MAGIC, 15)

/********the following commands are added for inteerupt handling. Jim on 26-03-2009********/
#define OP_SET_5331_CSR	            _IO( HYTEC_IOC_MAGIC, 16)
#define OP_SET_3331_CSR	            _IO( HYTEC_IOC_MAGIC, 17)
#define OP_REGISTER_PROCESSID	    _IO( HYTEC_IOC_MAGIC, 18)     	//for signal type SIGUSR1 and SIGUSR2
#define OP_BASE_ADD3	            _IO( HYTEC_IOC_MAGIC, 19)     	//for getting Base Add3 that is used by user space memory mapping
#define OP_BASE_ADD2	            _IO( HYTEC_IOC_MAGIC, 20)     	//for getting Base Add2 that is used by user space memory mapping
#define OP_BASE_ADD1	            _IO( HYTEC_IOC_MAGIC, 21)     	//for getting Base Add1 that is used by user space memory mapping
#define OP_DISABLE_INTERRUPT        _IO( HYTEC_IOC_MAGIC, 22)
#define OP_REGISTER_VECTOR        	_IO( HYTEC_IOC_MAGIC, 23)		//for multiple interrupts support

typedef struct
{
	unsigned short	sHardwareID;	
} IOCTL_CONFIG;


typedef struct
{
	unsigned long	sData;	
  	unsigned long	lAddress;
	unsigned long 	lLength;
} IOCTL_BUF;


typedef struct
{
	unsigned long	sData;	
  	unsigned long	lSlot;
  	unsigned long	lSite;
  	unsigned long	lMemAccess;
  	unsigned long	lAddress;
	unsigned long 	lLength;
} IOCTL_VME_BUF;




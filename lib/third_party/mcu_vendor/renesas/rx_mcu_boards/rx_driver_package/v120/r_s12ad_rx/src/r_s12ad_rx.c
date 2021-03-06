/***********************************************************************************************************************
* DISCLAIMER
* This software is supplied by Renesas Electronics Corporation and is only intended for use with Renesas products. No 
* other uses are authorized. This software is owned by Renesas Electronics Corporation and is protected under all 
* applicable laws, including copyright laws. 
* THIS SOFTWARE IS PROVIDED "AS IS" AND RENESAS MAKES NO WARRANTIES REGARDING
* THIS SOFTWARE, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY, 
* FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. ALL SUCH WARRANTIES ARE EXPRESSLY DISCLAIMED. TO THE MAXIMUM 
* EXTENT PERMITTED NOT PROHIBITED BY LAW, NEITHER RENESAS ELECTRONICS CORPORATION NOR ANY OF ITS AFFILIATED COMPANIES 
* SHALL BE LIABLE FOR ANY DIRECT, INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES FOR ANY REASON RELATED TO THIS 
* SOFTWARE, EVEN IF RENESAS OR ITS AFFILIATES HAVE BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
* Renesas reserves the right, without notice, to make changes to this software and to discontinue the availability of 
* this software. By using this software, you agree to the additional terms and conditions found by accessing the 
* following link:
* http://www.renesas.com/disclaimer 
*
* Copyright (C) 2013 Renesas Electronics Corporation. All rights reserved.
***********************************************************************************************************************/
/***********************************************************************************************************************
* File Name    : r_s12ad_rx.c
* Description  : Primary source code for 12-bit A/D Converter driver.
***********************************************************************************************************************/
/**********************************************************************************************************************
* History : DD.MM.YYYY Version Description
*           22.07.2013 1.00    Initial Release.
*           21.04.2014 1.20    Updated for RX210 advanced features; RX110/63x support.
*           06.11.2014 1.40    Added RX113 support.
*           01.03.2016 2.11    Added RX130 and RX230 support.
*           01.10.2016 2.20    Added RX65x support.
*                              Corresponding to the notice for A/D convert stop.(RX110/RX111/RX113/RX210/RX63x)
*                              Corresponding to the notice for shift to Low power mode.(RX110/RX111/RX113/RX210/RX63x)
*                              Delete parameter check by the enum value.
*                              R_ADC_ReadAll() parameter change.(adjust the parameter structure to RX130/RX230/RX231)
*                              Bug fix for callback function execute when pointer is NULL.(RX130/RX230/RX231)
*                              TEMPS register can change only during temperature sensor mode.(RX63x/RX210)
*           03.09.2018 3.00    Added RX66T support.
*                              Added the comment to while statement.
*           28.12.2018 3.10    Added RX72T support.
*           05.04.2019 4.00    Added support for GNUC and ICCRX.
*                              Deleted the process of RX210, RX631, and RX63N.
*                              Deleted the function of adc_gbadi_isr.
***********************************************************************************************************************/

/***********************************************************************************************************************
Includes   <System Includes> , "Project Includes"
***********************************************************************************************************************/
/* Includes board and MCU related header files. */
#include "platform.h"
/* Configuration for this package. */
#include "r_s12ad_rx_config.h"
/* Public interface header file for this package. */
#include "r_s12ad_rx_if.h"
/* Private header file for this package. */
#include "r_s12ad_rx_private.h"

/***********************************************************************************************************************
Macro definitions
***********************************************************************************************************************/

/***********************************************************************************************************************
Typedef definitions
***********************************************************************************************************************/

/***********************************************************************************************************************
Private global variables and functions
***********************************************************************************************************************/
#if (defined(BSP_MCU_RX64M) || defined(BSP_MCU_RX65_ALL) || defined(BSP_MCU_RX66T) \
  || defined(BSP_MCU_RX71M) || defined(BSP_MCU_RX72T))

/* In ROM */
extern R_BSP_VOLATILE_EVENACCESS uint16_t * const  dreg0_ptrs[];
extern R_BSP_VOLATILE_EVENACCESS uint16_t * const  dreg1_ptrs[];
#if (defined(BSP_MCU_RX66T) || defined(BSP_MCU_RX72T))
extern R_BSP_VOLATILE_EVENACCESS uint16_t * const  dreg2_ptrs[];
#endif

#else  // rx110/rx111/rx113/rx130/rx230/rx231

extern R_BSP_VOLATILE_EVENACCESS uint16_t * const  dreg_ptrs[]; // In ROM
adc_ctrl_t g_dcb = { ADC_MODE_MAX, false, NULL};  // In RAM

#endif


void adc_enable_s12adi0(void);


/******************************************************************************
* Function Name: R_ADC_Open
* Description  : This function applies power to the A/D peripheral, sets the
*                operational mode, trigger sources, interrupt priority, and
*                configurations common to all channels and sensors. If interrupt
*                priority is non-zero, the function takes a callback function
*                pointer for notifying the user at interrupt level whenever a
*                scan has completed.
*
* Arguments    : unit -
*                    Unit number
*                mode-
*                    Operational mode (see enumeration below)
*                p_cfg-
*                    Pointer to configuration structure (see below)
*                p_callback-
*                    Optional pointer to function called from interrupt when
*                    a scan completes
* Return Value : ADC_SUCCESS-
*                    Successful
*                ADC_ERR_AD_LOCKED-
*                    Open() call is in progress elsewhere
*                ADC_ERR_AD_NOT_CLOSED-
*                    Peripheral is still running in another mode; Perform
*                    R_ADC_Close() first
*                ADC_ERR_INVALID_ARG-
*                    mode or element of p_cfg structure has invalid value.
*                ADC_ERR_ILLEGAL_ARG-
*                    an argument is illegal based upon mode
*                ADC_ERR_MISSING_PTR-
*                    p_cfg pointer is FIT_NO_PTR/NULL
*******************************************************************************/
adc_err_t R_ADC_Open(uint8_t const          unit,
                     adc_mode_t const       mode,
                     adc_cfg_t * const      p_cfg,
                     void         (* const  p_callback)(void *p_args))
{
    return adc_open(unit, mode, p_cfg, p_callback);
} /* End of function R_ADC_Open() */


/******************************************************************************
* Function Name: R_ADC_Control
* Description  : This function provides commands for enabling channels and
*                sensors and for runtime operations. These include enabling/
*                disabling trigger sources and interrupts, initiating a
*                software trigger, and checking for scan completion.
*
* NOTE: Enabling a channel or a sensor, or setting the sample state count reg
*       cannot be done while the ADCSR.ADST bit is set (conversion in progress).
*       Because these commands should only be called once during initialization
*       before triggers are enabled, this should not be an issue. Registers
*       with this restriction include ADANSA, ADANSB, ADADS, ADADC, ADSSTR,
*       ADEXICR, and some bits in ADCSR and TSCR.
*       No runtime operational sequence checking of any kind is performed.
*
* Arguments    : unit -
*                    Unit number
*                cmd-
*                    Command to run
*                p_args-
*                    Pointer to optional configuration structure
* Return Value : ADC_SUCCESS-
*                    Successful
*                ADC_ERR_MISSING_PTR-
*                    p_args pointer is FIT_NO_PTR/NULL when required as an argument
*                ADC_ERR_INVALID_ARG-
*                    cmd or element of p_args structure has invalid value.
*                ADC_ERR_ILLEGAL_CMD-
*                    cmd is illegal based upon mode
*                ADC_ERR_SCAN_NOT_DONE-
*                    The requested scan has not completed
*                ADC_ERR_UNKNOWN
*                    Did not receive expected hardware response
*******************************************************************************/
adc_err_t R_ADC_Control(uint8_t const       unit,
                        adc_cmd_t const     cmd,
                        void * const        p_args)
{
    return adc_control(unit, cmd, p_args);
} /* End of function R_ADC_Control() */


/******************************************************************************
* Function Name: R_ADC_Read
* Description  : This function reads conversion results from a single channel,
*                sensor, or the double trigger register.
* Arguments    : unit -
*                    Unit number
*                reg_id-
*                    Id for the register to read (see enum below)
*                p_data-
*                    Pointer to variable to load value into.
* Return Value : ADC_SUCCESS-
*                    Successful
*                ADC_ERR_INVALID_ARG-
*                    reg_id contains an invalid value.
*                ADC_ERR_MISSING _PTR-
*                    p_data is FIT_NO_PTR/NULL
*******************************************************************************/
adc_err_t R_ADC_Read(uint8_t            unit,
                     adc_reg_t const    reg_id,
                     uint16_t * const   p_data)
{
    dregs_t *p_dregs;

#if (defined(BSP_MCU_RX64M) || defined(BSP_MCU_RX65_ALL) || defined(BSP_MCU_RX66T) \
  || defined(BSP_MCU_RX71M) || defined(BSP_MCU_RX72T))
    p_dregs = GET_DATA_ARR(unit);
#else
    p_dregs = dreg_ptrs;
#endif

#if ADC_CFG_PARAM_CHECKING_ENABLE == 1
    #if (defined(BSP_MCU_RX64M) || defined(BSP_MCU_RX65_ALL) || defined(BSP_MCU_RX71M))
    if (unit > 1)
    {
        return ADC_ERR_INVALID_ARG;
    }
    #elif (defined(BSP_MCU_RX66T) || defined(BSP_MCU_RX72T))
    if (unit > 2)
    {
        return ADC_ERR_INVALID_ARG;
    }
    #endif

    /* Casting is valid because it matches the type to the right side or argument. */
    if ((NULL == p_data) || (FIT_NO_PTR == p_data))
    {
        return ADC_ERR_MISSING_PTR;
    }

    if (NULL == p_dregs[reg_id])
    {
        return ADC_ERR_INVALID_ARG;
    }

    /* Do not check if corresponding channel/sensor is enabled */
#endif

    *p_data = *p_dregs[reg_id];

    return ADC_SUCCESS;
} /* End of function R_ADC_Read() */

/******************************************************************************
* Function Name: R_ADC_ReadAll
* Description  : This function reads conversion results from all potential
*                sources, enabled or not.
* Arguments    : p_all_data-
*                    Pointer to structure to load register values into.
* Return Value : ADC_SUCCESS-
*                    Successful
*                ADC_ERR_MISSING _PTR-
*                    p_data is FIT_NO_PTR/NULL
*******************************************************************************/
adc_err_t R_ADC_ReadAll(adc_data_t * const  p_all_data)
{
#if ADC_CFG_PARAM_CHECKING_ENABLE == 1
    /* Casting is valid because it matches the type to the right side or argument. */
    if ((NULL == p_all_data) || (FIT_NO_PTR == p_all_data))
    {
        return ADC_ERR_MISSING_PTR;
    }
#endif

#if (defined(BSP_MCU_RX64M) || defined(BSP_MCU_RX65_ALL) || defined(BSP_MCU_RX66T) \
  || defined(BSP_MCU_RX71M) || defined(BSP_MCU_RX72T)    || defined(BSP_MCU_RX231) \
  || defined(BSP_MCU_RX230) || defined(BSP_MCU_RX130))

    return adc_read_all(p_all_data);

#else // rx110/rx111/rx113

    p_all_data->chan[ADC_REG_CH0] = S12AD.ADDR0;
    p_all_data->chan[ADC_REG_CH1] = S12AD.ADDR1;
    p_all_data->chan[ADC_REG_CH2] = S12AD.ADDR2;
    p_all_data->chan[ADC_REG_CH3] = S12AD.ADDR3;
    p_all_data->chan[ADC_REG_CH4] = S12AD.ADDR4;
    p_all_data->chan[ADC_REG_CH6] = S12AD.ADDR6;
    p_all_data->chan[ADC_REG_CH8] = S12AD.ADDR8;
    p_all_data->chan[ADC_REG_CH9] = S12AD.ADDR9;
    p_all_data->chan[ADC_REG_CH10] = S12AD.ADDR10;
    p_all_data->chan[ADC_REG_CH11] = S12AD.ADDR11;
    p_all_data->chan[ADC_REG_CH12] = S12AD.ADDR12;
    p_all_data->chan[ADC_REG_CH13] = S12AD.ADDR13;
    p_all_data->chan[ADC_REG_CH14] = S12AD.ADDR14;
    p_all_data->chan[ADC_REG_CH15] = S12AD.ADDR15;

#if (!defined(BSP_MCU_RX110) && !defined(BSP_MCU_RX111))
    p_all_data->chan[ADC_REG_CH5] = S12AD.ADDR5;
    p_all_data->chan[ADC_REG_CH7] = S12AD.ADDR7;
#endif
#ifdef BSP_MCU_RX113
    p_all_data->chan[ADC_REG_CH21] = S12AD.ADDR21;
#endif
    p_all_data->temp = S12AD.ADTSDR;
    p_all_data->volt = S12AD.ADOCDR;

    return ADC_SUCCESS;

#endif /* rx110/rx111/rx113 */
} /* End of function R_ADC_ReadAll() */

/******************************************************************************
* Function Name: R_ADC_Close
* Description  : This function ends any scan in progress, disables interrupts,
*                and removes power to the A/D peripheral.
* Arguments    : unit - Unit number
* Return Value : ADC_SUCCESS - Successful
*                ADC_ERR_INVALID_ARG - unit contains an invalid value.
*******************************************************************************/
adc_err_t   R_ADC_Close(uint8_t const unit)
{
#if (defined(BSP_MCU_RX64M) || defined(BSP_MCU_RX65_ALL) || defined(BSP_MCU_RX66T) \
  || defined(BSP_MCU_RX71M) || defined(BSP_MCU_RX72T)    || defined(BSP_MCU_RX231) \
  || defined(BSP_MCU_RX230) || defined(BSP_MCU_RX130))

    return adc_close(unit);

#else // rx110/rx111/rx113
    volatile uint16_t i;

#if ADC_CFG_PARAM_CHECKING_ENABLE == 1
    if (unit > 0)
    {
        return ADC_ERR_INVALID_ARG;
    }
#endif

    /* Stop triggers & conversions, and disable peripheral interrupts */
    R_BSP_InterruptRequestDisable(VECT(S12AD,S12ADI0));         // disable interrupts in ICU
    IR(S12AD,S12ADI0) = 0;          // clear interrupt flag
    R_BSP_InterruptRequestDisable(VECT(S12AD,GBADI));           // disable interrupts in ICU
    IR(S12AD,GBADI) = 0;            // clear interrupt flag
    S12AD.ADCSR.BIT.TRGE = 0;
    S12AD.ADCSR.WORD = 0;

    /* Wait for 2 ADCLK cycles (MAX: 128 ICLK cycles) */
    /* WAIT_LOOP */
    for (i = 0; i < 128; i++)
    {
        R_BSP_NOP();
    }

    /* Power down peripheral */
    R_BSP_RegisterProtectDisable(BSP_REG_PROTECT_LPC_CGC_SWR);
#ifndef BSP_MCU_RX11_ALL             // RX63x
    if (g_dcb.mode == ADC_MODE_SS_TEMPERATURE)
    {
        TEMPS.TSCR.BYTE = 0;
        MSTP(TEMPS) = 1;
    }
#endif
    MSTP(S12AD) = 1;
    R_BSP_RegisterProtectEnable(BSP_REG_PROTECT_LPC_CGC_SWR);

    /* Show driver as closed */
    g_dcb.opened = false;

    return ADC_SUCCESS;

#endif /* rx110/rx111/rx113 */
} /* End of function R_ADC_Close() */


/*****************************************************************************
* Function Name: R_ADC_GetVersion
* Description  : Returns the version of this module. The version number is
*                encoded such that the top two bytes are the major version
*                number and the bottom two bytes are the minor version number.
* Arguments    : none
* Return Value : version number
******************************************************************************/
uint32_t  R_ADC_GetVersion(void)
{
    uint32_t const version = (ADC_VERSION_MAJOR << 16) | ADC_VERSION_MINOR;

    return version;
} /* End of function R_ADC_GetVersion() */


#if (!defined(BSP_MCU_RX64M) && !defined(BSP_MCU_RX65_ALL) && !defined(BSP_MCU_RX66T) \
  && !defined(BSP_MCU_RX71M) && !defined(BSP_MCU_RX72T))

/******************************************************************************
* Function Name: adc_enable_s12adi0
* Description  : This function clears the S12ADI0 interrupt flag and enables
*                interrupts in the peripheral (for IR flag usage). If priority
*                is not 0, interrupts are enabled in the ICU.
*                NOTE: This has nothing to do with enabling triggers.
* Arguments    : none
* Return Value : none
*******************************************************************************/
void adc_enable_s12adi0(void)
{

    IR(S12AD,S12ADI0) = 0;                  // clear flag
    S12AD.ADCSR.BIT.ADIE = 1;               // enable in peripheral
    if (ICU.IPR[IPR_S12AD_S12ADI0].BYTE != 0)
    {
        R_BSP_InterruptRequestEnable(VECT(S12AD,S12ADI0));             // enable in ICU
    }
} /* End of function adc_enable_s12adi0() */


/*****************************************************************************
* Function Name: adc_s12adi0_isr
* Description  : Interrupt handler for normal/Group A/double trigger
*                scan complete.
* Arguments    : none
* Return Value : none
******************************************************************************/
R_BSP_PRAGMA_STATIC_INTERRUPT(adc_s12adi0_isr, VECT(S12AD,S12ADI0))
R_BSP_ATTRIB_STATIC_INTERRUPT void adc_s12adi0_isr(void)
{
    adc_cb_evt_t    event=ADC_EVT_SCAN_COMPLETE;

    // presence of callback function verified in Open()
    if ((g_dcb.callback != NULL) && (g_dcb.callback != FIT_NO_FUNC))
    {
        g_dcb.callback(&event);
    }
} /* End of function adc_s12adi0_isr() */

#endif /* #if (!defined(BSP_MCU_RX64M) && !defined(BSP_MCU_RX65_ALL) && !defined(BSP_MCU_RX66T) &&
               !defined(BSP_MCU_RX71M) && !defined(BSP_MCU_RX72T)) */


/*
 * SPDX-FileCopyrightText: 2015-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*******************************************************************************
 * NOTICE
 * The hal is not public api, don't use in application code.
 * See readme.md in hal/include/hal/readme.md
 ******************************************************************************/

/*
 * The HAL layer for SPI Slave HD mode.
 *
 * Usage (segment mode):
 * - Firstly, initialize the slave with `spi_slave_hd_hal_init`
 *
 * - Event handling:
 *     - (Optional) Call ``spi_slave_hd_hal_enable_event_intr`` to enable the used interrupts
 *     - (Basic) Call ``spi_slave_hd_hal_check_clear_event`` to check whether an event happen, and also
 *       clear its interrupt. For events: SPI_EV_BUF_TX, SPI_EV_BUF_RX, SPI_EV_BUF_RX, SPI_EV_CMD9,
 *       SPI_EV_CMDA.
 *     - (Advanced) Call ``spi_slave_hd_hal_check_disable_event`` to disable the interrupt of an event,
 *       so that the task can call ``spi_slave_hd_hal_invoke_event_intr`` later to manually invoke the
 *       ISR. For SPI_EV_SEND, SPI_EV_RECV.
 *
 * - TXDMA:
 *     - To send data through DMA, call `spi_slave_hd_hal_txdma`
 *     - When the operation is done, SPI_EV_SEND will be triggered.
 *
 * - RXDMA:
 *     - To receive data through DMA, call `spi_slave_hd_hal_rxdma`
 *     - When the operation is done, SPI_EV_RECV will be triggered.
 *     - Call ``spi_slave_hd_hal_rxdma_seg_get_len`` to get the received length
 *
 *  - Shared buffer:
 *      - Call ``spi_slave_hd_hal_write_buffer`` to write the shared register buffer. When the buffer is
 *        read by the master (regardless of the read address), SPI_EV_BUF_TX will be triggered
 *      - Call ``spi_slave_hd_hal_read_buffer`` to read the shared register buffer. When the buffer is
 *        written by the master (regardless of the written address), SPI_EV_BUF_RX will be triggered.
 */

#pragma once

#include "esp_types.h"
#include "esp_err.h"
#include "soc/soc_caps.h"
#if SOC_GDMA_SUPPORTED
#include "soc/gdma_channel.h"
#endif
#include "hal/spi_types.h"
#include "hal/dma_types.h"
#if SOC_GPSPI_SUPPORTED
#include "hal/spi_ll.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if SOC_GPSPI_SUPPORTED

//NOTE!! If both A and B are not defined, '#if (A==B)' is true, because GCC use 0 stand for undefined symbol
#if !defined(SOC_GDMA_TRIG_PERIPH_SPI2_BUS)
typedef dma_descriptor_align4_t spi_dma_desc_t;
#else
#if defined(SOC_GDMA_BUS_AXI) && (SOC_GDMA_TRIG_PERIPH_SPI2_BUS == SOC_GDMA_BUS_AXI)
typedef dma_descriptor_align8_t spi_dma_desc_t;
#elif defined(SOC_GDMA_BUS_AHB) && (SOC_GDMA_TRIG_PERIPH_SPI2_BUS == SOC_GDMA_BUS_AHB)
typedef dma_descriptor_align4_t spi_dma_desc_t;
#endif
#endif

/**
 * @brief Type of dma descriptor with appended members
 *        this structure inherits DMA descriptor, with a pointer to the transaction descriptor passed from users.
 */
typedef struct {
    spi_dma_desc_t  *desc;                            ///< DMA descriptor
    void            *arg;                             ///< This points to the transaction descriptor user passed in
} spi_slave_hd_hal_desc_append_t;

/// Configuration of the HAL
typedef struct {
    uint32_t      host_id;                          ///< Host ID of the spi peripheral
    bool          dma_enabled;                      ///< DMA enabled or not
    bool          append_mode;                      ///< True for DMA append mode, false for segment mode
    uint32_t      spics_io_num;                     ///< CS GPIO pin for this device
    uint8_t       mode;                             ///< SPI mode (0-3)
    uint32_t      command_bits;                     ///< command field bits, multiples of 8 and at least 8.
    uint32_t      address_bits;                     ///< address field bits, multiples of 8 and at least 8.
    uint32_t      dummy_bits;                       ///< dummy field bits, multiples of 8 and at least 8.

    struct {
        uint32_t  tx_lsbfirst : 1;                  ///< Whether TX data should be sent with LSB first.
        uint32_t  rx_lsbfirst : 1;                  ///< Whether RX data should be read with LSB first.
    };
} spi_slave_hd_hal_config_t;

/// Context of the HAL, initialized by :cpp:func:`spi_slave_hd_hal_init`.
typedef struct {
    /* These two need to be malloced by the driver first */
    spi_slave_hd_hal_desc_append_t  *dmadesc_tx;            ///< Head of the TX DMA descriptors.
    spi_slave_hd_hal_desc_append_t  *dmadesc_rx;            ///< Head of the RX DMA descriptors.

    /* address of the hardware */
    spi_dev_t                       *dev;                   ///< Beginning address of the peripheral registers.
    bool                            dma_enabled;            ///< DMA enabled or not
    bool                            append_mode;            ///< True for DMA append mode, false for segment mode
    uint32_t                        dma_desc_num;           ///< Number of the available DMA descriptors. Calculated from ``bus_max_transfer_size``.
    uint32_t                        current_eof_addr;
    spi_slave_hd_hal_desc_append_t  *tx_cur_desc;           ///< Current TX DMA descriptor that could be linked (set up).
    spi_slave_hd_hal_desc_append_t  *tx_dma_head;           ///< Head of the linked TX DMA descriptors which are not used by hardware
    spi_slave_hd_hal_desc_append_t  *tx_dma_tail;           ///< Tail of the linked TX DMA descriptors which are not used by hardware
    uint32_t                        tx_used_desc_cnt;       ///< Number of the TX descriptors that have been setup
    spi_slave_hd_hal_desc_append_t  *rx_cur_desc;           ///< Current RX DMA descriptor that could be linked (set up).
    spi_slave_hd_hal_desc_append_t  *rx_dma_head;           ///< Head of the linked RX DMA descriptors which are not used by hardware
    spi_slave_hd_hal_desc_append_t  *rx_dma_tail;           ///< Tail of the linked RX DMA descriptors which are not used by hardware
    uint32_t                        rx_used_desc_cnt;       ///< Number of the RX descriptors that have been setup

    /* Internal status used by the HAL implementation, initialized as 0. */
    uint32_t                        intr_not_triggered;
} spi_slave_hd_hal_context_t;

/**
 * @brief Initialize the hardware and part of the context
 *
 * @param hal        Context of the HAL layer
 * @param hal_config Configuration of the HAL
 */
void spi_slave_hd_hal_init(spi_slave_hd_hal_context_t *hal, const spi_slave_hd_hal_config_t *hal_config);

/**
 * @brief Check and clear signal of one event
 *
 * @param hal       Context of the HAL layer
 * @param ev        Event to check
 * @return          True if event triggered, otherwise false
 */
bool spi_slave_hd_hal_check_clear_event(spi_slave_hd_hal_context_t* hal, spi_event_t ev);

/**
 * @brief Check and clear the interrupt of one event.
 *
 * @note The event source will be kept, so that the interrupt can be invoked by
 *       :cpp:func:`spi_slave_hd_hal_invoke_event_intr`. If event not triggered, its interrupt source
 *       will not be disabled either.
 *
 * @param hal       Context of the HAL layer
 * @param ev        Event to check and disable
 * @return          True if event triggered, otherwise false
 */
bool spi_slave_hd_hal_check_disable_event(spi_slave_hd_hal_context_t* hal, spi_event_t ev);

/**
 * @brief Enable to involve the ISR of corresponding event.
 *
 * @note The function, compared with :cpp:func:`spi_slave_hd_hal_enable_event_intr`, contains a
 *       workaround to force trigger the interrupt, even if the interrupt source cannot be initialized
 *       correctly.
 *
 * @param hal       Context of the HAL layer
 * @param ev        Event (reason) to invoke the ISR
 */
void spi_slave_hd_hal_invoke_event_intr(spi_slave_hd_hal_context_t* hal, spi_event_t ev);

/**
 * @brief Enable the interrupt source of corresponding event.
 *
 * @param hal       Context of the HAL layer
 * @param ev        Event whose corresponding interrupt source should be enabled.
 */
void spi_slave_hd_hal_enable_event_intr(spi_slave_hd_hal_context_t* hal, spi_event_t ev);

////////////////////////////////////////////////////////////////////////////////
// RX DMA
////////////////////////////////////////////////////////////////////////////////
/**
 * @brief Start the RX DMA operation to the specified buffer.
 *
 * @param hal       Context of the HAL layer
 * @param[out] out_buf  Buffer to receive the data
 * @param len       Maximul length to receive
 */
void spi_slave_hd_hal_rxdma(spi_slave_hd_hal_context_t *hal);

/**
 * @brief Get the length of total received data
 *
 * @param hal       Context of the HAL layer
 * @return          The received length
 */
int spi_slave_hd_hal_rxdma_seg_get_len(spi_slave_hd_hal_context_t *hal);

/**
 * @brief Prepare hardware for a new dma rx trans
 *
 * @param hal       Context of the HAL layer
 */
void spi_slave_hd_hal_hw_prepare_rx(spi_slave_hd_hal_context_t *hal);

////////////////////////////////////////////////////////////////////////////////
// TX DMA
////////////////////////////////////////////////////////////////////////////////
/**
 * @brief Start the TX DMA operation with the specified buffer
 *
 * @param hal       Context of the HAL layer
 * @param data      Buffer of data to send
 * @param len       Size of the buffer, also the maximum length to send
 */
void spi_slave_hd_hal_txdma(spi_slave_hd_hal_context_t *hal);

/**
 * @brief Prepare hardware for a new dma tx trans
 *
 * @param hal       Context of the HAL layer
 */
void spi_slave_hd_hal_hw_prepare_tx(spi_slave_hd_hal_context_t *hal);

////////////////////////////////////////////////////////////////////////////////
// Shared buffer
////////////////////////////////////////////////////////////////////////////////
/**
 * @brief Read from the shared register buffer
 *
 * @param hal       Context of the HAL layer
 * @param addr      Address of the shared register to read
 * @param out_data  Buffer to store the read data
 * @param len       Length to read from the shared buffer
 */
void spi_slave_hd_hal_read_buffer(spi_slave_hd_hal_context_t *hal, int addr, uint8_t *out_data, size_t len);

/**
 * @brief Write the shared register buffer
 *
 * @param hal       Context of the HAL layer
 * @param addr      Address of the shared register to write
 * @param data      Buffer of the data to write
 * @param len       Length to write into the shared buffer
 */
void spi_slave_hd_hal_write_buffer(spi_slave_hd_hal_context_t *hal, int addr, uint8_t *data, size_t len);

/**
 * @brief Get the length of previous transaction.
 *
 * @param hal       Context of the HAL layer
 * @return          The length of previous transaction
 */
int spi_slave_hd_hal_get_rxlen(spi_slave_hd_hal_context_t *hal);

/**
 * @brief Get the address of last transaction
 *
 * @param hal       Context of the HAL layer
 * @return          The address of last transaction
 */
int spi_slave_hd_hal_get_last_addr(spi_slave_hd_hal_context_t *hal);


////////////////////////////////////////////////////////////////////////////////
// Append Mode
////////////////////////////////////////////////////////////////////////////////
/**
 * @brief Return the finished TX transaction
 *
 * @note This API is based on this assumption: the hardware behaviour of current transaction completion is only modified by the its own caller layer.
 * This means if some other code changed the hardware behaviour (e.g. clear intr raw bit), or the caller call this API without noticing the HW behaviour,
 * this API will go wrong.
 *
 * @param hal            Context of the HAL layer
 * @param out_trans      Pointer to the caller-defined transaction
 * @param real_buff_addr Actually data buffer head the HW used
 * @return               1: Transaction is finished; 0: Transaction is not finished
 */
bool spi_slave_hd_hal_get_tx_finished_trans(spi_slave_hd_hal_context_t *hal, void **out_trans, void **real_buff_addr);

/**
 * @brief Return the finished RX transaction
 *
 * @note This API is based on this assumption: the hardware behaviour of current transaction completion is only modified by the its own caller layer.
 * This means if some other code changed the hardware behaviour (e.g. clear intr raw bit), or the caller call this API without noticing the HW behaviour,
 * this API will go wrong.
 *
 * @param hal            Context of the HAL layer
 * @param out_trans      Pointer to the caller-defined transaction
 * @param real_buff_addr Actually data buffer head the HW used
 * @param out_len        Actual number of bytes of received data
 * @return               1: Transaction is finished; 0: Transaction is not finished
 */
bool spi_slave_hd_hal_get_rx_finished_trans(spi_slave_hd_hal_context_t *hal, void **out_trans, void **real_buff_addr, size_t *out_len);

/**
 * @brief Load the TX DMA descriptors without stopping the DMA
 *
 * @param hal            Context of the HAL layer
 * @param data           Buffer of the transaction data
 * @param len            Length of the data
 * @param arg            Pointer used by the caller to indicate the transaction. Will be returned by ``spi_slave_hd_hal_get_tx_finished_trans`` when transaction is finished
 * @return
 *        - ESP_OK: on success
 *        - ESP_ERR_INVALID_STATE: Function called in invalid state.
 */
esp_err_t spi_slave_hd_hal_txdma_append(spi_slave_hd_hal_context_t *hal, uint8_t *data, size_t len, void *arg);

/**
 * @brief Load the RX DMA descriptors without stopping the DMA
 *
 * @param hal            Context of the HAL layer
 * @param data           Buffer of the transaction data
 * @param len            Length of the data
 * @param arg            Pointer used by the caller to indicate the transaction. Will be returned by ``spi_slave_hd_hal_get_rx_finished_trans`` when transaction is finished
 * @return
 *        - ESP_OK: on success
 *        - ESP_ERR_INVALID_STATE: Function called in invalid state.
 */
esp_err_t spi_slave_hd_hal_rxdma_append(spi_slave_hd_hal_context_t *hal, uint8_t *data, size_t len, void *arg);

#endif  //#if SOC_GPSPI_SUPPORTED

#ifdef __cplusplus
}
#endif

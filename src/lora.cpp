/**
 * @file lora.cpp
 * @author Bernd Giesecke (bernd.giesecke@rakwireless.com)
 * @brief LoRaWAN initialization & handler
 * @version 0.1
 * @date 2021-09-15
 * 
 * @copyright Copyright (c) 2021
 * 
 */
#ifdef NRF52_SERIES

#include "WisBlock-API.h"

// LoRa callbacks
static RadioEvents_t RadioEvents;
void on_tx_done(void);
void on_rx_done(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);
void on_tx_timeout(void);
void on_rx_timeout(void);
void on_rx_crc_error(void);
void on_cad_done(bool cadResult);

/**
 * @brief Initialize LoRa HW and LoRaWan MAC layer
 * 
 * @return int8_t result
 *  0 => OK
 * -1 => SX126x HW init failure
 */
int8_t init_lora(void)
{
	if (!g_lorawan_initialized)
	{
		// Initialize LoRa chip.
		if (lora_rak4630_init() != 0)
		{
			API_LOG("LORA", "Failed to initialize SX1262");
			return -1;
		}

		// Initialize the Radio
		RadioEvents.TxDone = on_tx_done;
		RadioEvents.RxDone = on_rx_done;
		RadioEvents.TxTimeout = on_tx_timeout;
		RadioEvents.RxTimeout = on_rx_timeout;
		RadioEvents.RxError = on_rx_crc_error;
		RadioEvents.CadDone = on_cad_done;

		Radio.Init(&RadioEvents);
	}
	Radio.Sleep(); // Radio.Standby();

	Radio.SetChannel(g_lorawan_settings.p2p_frequency);

	Radio.SetTxConfig(MODEM_LORA, g_lorawan_settings.p2p_tx_power, 0, g_lorawan_settings.p2p_bandwidth,
					  g_lorawan_settings.p2p_sf, g_lorawan_settings.p2p_cr,
					  g_lorawan_settings.p2p_preamble_len, false,
					  true, 0, 0, false, 5000);

	Radio.SetRxConfig(MODEM_LORA, g_lorawan_settings.p2p_bandwidth, g_lorawan_settings.p2p_sf,
					  g_lorawan_settings.p2p_cr, 0, g_lorawan_settings.p2p_preamble_len,
					  g_lorawan_settings.p2p_symbol_timeout, false,
					  0, true, 0, 0, false, true);

	if (g_lorawan_settings.send_repeat_time != 0)
	{
		// LoRa is setup, start the timer that will wakeup the loop frequently
		g_task_wakeup_timer.begin(g_lorawan_settings.send_repeat_time, periodic_wakeup);
		g_task_wakeup_timer.start();
	}

	Radio.Rx(0);

	digitalWrite(LED_BUILTIN, LOW);

	g_lorawan_initialized = true;

	API_LOG("LORA", "LoRa initialized");
	return 0;
}

/**
 * @brief Function to be executed on Radio Tx Done event
 */
void on_tx_done(void)
{
	API_LOG("LORA", "TX finished");
	g_rx_fin_result = true;
	// Wake up task to send initial packet
	g_task_event_type |= LORA_TX_FIN;
	// Notify task about the event
	if (g_task_sem != NULL)
	{
		API_LOG("LORA", "Waking up loop task");
		xSemaphoreGive(g_task_sem);
	}
	Radio.Rx(0);
}

/**@brief Function to be executed on Radio Rx Done event
 */
void on_rx_done(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
	API_LOG("LORA", "LoRa Packet received with size:%d, rssi:%d, snr:%d",
			size, rssi, snr);

	g_last_rssi = rssi;
	g_last_snr = snr;

	// Copy the data into loop data buffer
	memcpy(g_rx_lora_data, payload, size);
	g_rx_data_len = size;
	g_task_event_type |= LORA_DATA;
	// Notify task about the event
	if (g_task_sem != NULL)
	{
		API_LOG("LORA", "Waking up loop task");
		xSemaphoreGive(g_task_sem);
	}

	Radio.Rx(0);
}

/**@brief Function to be executed on Radio Tx Timeout event
 */
void on_tx_timeout(void)
{
	API_LOG("LORA", "TX timeout");
	g_rx_fin_result = false;
	// Wake up task to send initial packet
	g_task_event_type |= LORA_TX_FIN;
	// Notify task about the event
	if (g_task_sem != NULL)
	{
		API_LOG("LORA", "Waking up loop task");
		xSemaphoreGive(g_task_sem);
	}
	Radio.Rx(0);
}

/**@brief Function to be executed on Radio Rx Timeout event
 */
void on_rx_timeout(void)
{
	API_LOG("LORA", "OnRxTimeout");

	Radio.Rx(0);
}

/**@brief Function to be executed on Radio Rx Error event
 */
void on_rx_crc_error(void)
{
	Radio.Rx(0);
}

/**@brief Function to be executed on Radio Rx Error event
 */
void on_cad_done(bool cadResult)
{
	if (cadResult)
	{
		Radio.Rx(0);
	}
	else
	{
		Radio.Send(g_tx_lora_data, g_tx_data_len);
	}
}

/**
 * @brief Prepare packet to be sent and start CAD routine
 * 
 */
bool send_lora_packet(uint8_t *data, uint8_t size)
{
	if (size > 256)
	{
		return false;
	}
	g_tx_data_len = size;
	memcpy(g_tx_lora_data, data, size);

	// Prepare LoRa CAD
	Radio.Sleep();
	Radio.SetCadParams(LORA_CAD_08_SYMBOL, g_lorawan_settings.p2p_sf + 13, 10, LORA_CAD_ONLY, 0);

	// Switch on Indicator lights
	digitalWrite(LED_BUILTIN, HIGH);

	// Start CAD
	Radio.StartCad();

	return true;
}

#endif // NRF52_SERIES
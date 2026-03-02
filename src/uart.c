#include <EFM8LB1.h>
#include "uart.h"

#define TX_BUF_SIZE 64
static xdata char tx_buf[TX_BUF_SIZE];
static unsigned char tx_head = 0;
static unsigned char tx_tail = 0;
static bit tx_busy = 0;

#define RX_BUF_SIZE 64
static xdata char rx_buf[RX_BUF_SIZE];
static unsigned char rx_head = 0;
static unsigned char rx_tail = 0; 

void UART_init(void) {
    ES0 = 1;
    EA = 1;
}

void UART0_ISR(void) __interrupt(4) {
    if (RI) { // received a byte -> 1
        RI = 0;
        rx_buf[rx_head] = SBUF;
        rx_head = (rx_head + 1) % TX_BUF_SIZE;
    }

    if (TI) {
        TI = 0;
        if (tx_tail != tx_head) {
            SBUF = tx_buf[tx_tail];
            tx_tail = (tx_tail + 1) % TX_BUF_SIZE;
        }
        else {
            tx_busy = 0;
        }
    }
}

void UART_send_char(char c)
{
	// Wait if buffer full
	while (((tx_head + 1) % TX_BUF_SIZE) == tx_tail);

	EA = 0;
	tx_buf[tx_head] = c;
	tx_head = (tx_head + 1) % TX_BUF_SIZE;

	if (!tx_busy)
	{
		tx_busy = 1;
		TI = 1;   // Kick-start the ISR
	}
	EA = 1;
}

void UART_send_string(const char *s)
{
	while (*s)
	{
		UART_send_char(*s++);
	}
}

bit UART_available(void) {
    return (rx_head != rx_tail);
}

char UART_read(void) {
    char c;
    while (!UART_available());
    c = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) % RX_BUF_SIZE;
    return c;
}

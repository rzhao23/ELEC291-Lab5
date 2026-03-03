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

// ---- UART1: P3.0 = TX, P3.1 = RX, baud rate from Timer 1 ----

#define TX1_BUF_SIZE 64
static xdata char tx1_buf[TX1_BUF_SIZE];
static unsigned char tx1_head = 0;
static unsigned char tx1_tail = 0;
static bit tx1_busy = 0;

#define RX1_BUF_SIZE 64
static xdata char rx1_buf[RX1_BUF_SIZE];
static unsigned char rx1_head = 0;
static unsigned char rx1_tail = 0;

void UART1_init(void) {
    EIE1 |= 0x80; // enable UART1 interrupt (interrupt 15)
    RIE = 1;
    TIE = 1;
    EA  = 1;
}

void UART1_ISR(void) __interrupt(15) {
    if (RI1) {
        RI1 = 0;
        rx1_buf[rx1_head] = SBUF1;
        rx1_head = (rx1_head + 1) % RX1_BUF_SIZE;
    }
    if (TI1) {
        TI1 = 0;
        if (tx1_tail != tx1_head) {
            SBUF1 = tx1_buf[tx1_tail];
            tx1_tail = (tx1_tail + 1) % TX1_BUF_SIZE;
        } else {
            tx1_busy = 0;
        }
    }
}

void UART1_send_char(char c) {
    while (((tx1_head + 1) % TX1_BUF_SIZE) == tx1_tail);
    EA = 0;
    tx1_buf[tx1_head] = c;
    tx1_head = (tx1_head + 1) % TX1_BUF_SIZE;
    if (!tx1_busy) {
        tx1_busy = 1;
        TI1 = 1; // Kick-start the ISR
    }
    EA = 1;
}

void UART1_send_string(const char *s) {
    while (*s)
        UART1_send_char(*s++);
}

bit UART1_available(void) {
    return (rx1_head != rx1_tail);
}

char UART1_read(void) {
    char c;
    while (!UART1_available());
    c = rx1_buf[rx1_tail];
    rx1_tail = (rx1_tail + 1) % RX1_BUF_SIZE;
    return c;
}

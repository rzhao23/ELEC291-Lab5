#ifndef UART_H
#define UART_H

void UART_init(void);
void UART_send_char(char c);
void UART_send_string(const char *s);
char UART_read(void);
bit  UART_available(void);

#endif
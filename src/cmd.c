#include <EFM8LB1.h>
#include <string.h>
#include "cmd.h"
#include "uart.h"
#include "time.h"

void cmd_help(unsigned char argc, char *argv[]) reentrant {
	unsigned char i;
	UART_send_string("Available commands:\r\n");
    for (i = 0; i < cli_cmd_count; i++)
    {
        UART_send_string("  ");
        UART_send_string(cli_commands[i].name);
        UART_send_string(" - ");
        UART_send_string(cli_commands[i].help);
        UART_send_string("\n");
    }
}

void cmd_kill(unsigned char argc, char *argv[]) reentrant {

}

void cmd_read(unsigned char argc, char *argv[]) reentrant {

}

void cmd_mode(unsigned char argc, char *argv[]) __reentrant {

}

void cmd_round(unsigned char argc, char *argv[]) reentrant {

}

void cmd_info(unsigned char argc, char *argv[]) reentrant {
    UART_send_string("EFM8 Frequency measurement using Timer/Counter 0.\r\n");
    UART_send_string("File: ");
    UART_send_string(__FILE__);
    UART_send_string("\r\nCompiled: ");
    UART_send_string(__DATE__);
    UART_send_string(", ");
    UART_send_string(__TIME__);
    UART_send_string("\r\n");
    return;
}

void cmd_reset(unsigned char argc, char *argv[]) reentrant {
    UART_send_string("System Resetting...");
    RSTSRC = 0x10;
}

const cli_cmd_t code cli_commands[] = {
    {"tibo", "--help",  "Show all commands", cmd_help},
    {"tibo", "K", "Stop Read C/L", cmd_kill},
    {"tibo", "R","Start Read C/L", cmd_read},
    {"tibo", "-mode",  "Measure C or L", cmd_mode},
    {"tibo", "-round", "Round fragile", cmd_round},
    {"tibo", "info",   "Show device info", cmd_info},
    {"tibo", "--reset", "Reset the System", cmd_reset},
};

const unsigned char cli_cmd_count = 7;

void cli_parse_and_dispatch(char *input) {
	unsigned char argc = 0;
	static char * xdata argv[8];
	unsigned char i;
	char in_arg = 0;

	for (i = 0; input[i] != '\0'; i++) {
		if (input[i] == ' ') {
			input[i] = '\0';
			in_arg = 0;
		}
		else if (!in_arg) {
			if (argc < 8) {
				argv[argc++] = &input[i];
			}
			in_arg = 1;
		}
	}

	if (argc == 0) return;

	for (i = 0; i < cli_cmd_count; i++) {
        if (strcmp(argv[0], cli_commands[i].name) == 0)
        {
            cli_commands[i].handler(argc, argv);
            return;
        }
	}
	UART_send_string("Unknown command: ");
    UART_send_string(argv[1]);
    UART_send_string("\n");
}

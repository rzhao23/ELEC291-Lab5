#include <EFM8LB1.h>
#include <string.h>
#include "cmd.h"
#include "uart.h"

void cmd_help(unsigned char argc, char *argv[]) reentrant {
	unsigned char i;
	UART1_send_string("Usage: tibo <cmd> <args>\r\n");
	UART1_send_string("Available commands:\r\n");
    for (i = 0; i < cli_cmd_count; i++)
    {
        UART1_send_string("  tibo ");
        UART1_send_string(cli_commands[i].name);
        UART1_send_string(" - ");
        UART1_send_string(cli_commands[i].help);
        UART1_send_string("\r\n");
    }
}

void cmd_psize(unsigned char argc, char *argv[]) reentrant {
    if (argc < 2) {
        UART1_send_string("Usage: tibo psize -b|-s\r\n");
        return;
    }
    if (strcmp(argv[1], "-b") == 0) {
        UART_send_string("!psize:b\n");   // to Python via UART0
    } else if (strcmp(argv[1], "-s") == 0) {
        UART_send_string("!psize:s\n");   // to Python via UART0
    } else {
        UART1_send_string("Usage: tibo psize -b|-s\r\n");
    }
}

void cmd_enable(unsigned char argc, char *argv[]) __reentrant {
    if (argc < 2) {
        UART1_send_string("Usage: tibo enable -1|-2\r\n");
        return;
    }
    if (strcmp(argv[1], "-1") == 0) {
        UART_send_string("!ch1:on\n");
        UART1_send_string("CH1: enabled\r\n");
    } else if (strcmp(argv[1], "-2") == 0) {
        UART_send_string("!ch2:on\n");
        UART1_send_string("CH2: enabled\r\n");
    } else {
        UART1_send_string("Usage: tibo enable -1|-2\r\n");
    }
}

void cmd_disable(unsigned char argc, char *argv[]) reentrant {
    if (argc < 2) {
        UART1_send_string("Usage: tibo disable -1|-2\r\n");
        return;
    }
    if (strcmp(argv[1], "-1") == 0) {
        UART_send_string("!ch1:off\n");
        UART1_send_string("CH1: disabled\r\n");
    } else if (strcmp(argv[1], "-2") == 0) {
        UART_send_string("!ch2:off\n");
        UART1_send_string("CH2: disabled\r\n");
    } else {
        UART1_send_string("Usage: tibo disable -1|-2\r\n");
    }
}

void cmd_info(unsigned char argc, char *argv[]) reentrant {
    UART1_send_string("EFM8 Frequency measurement using Timer/Counter 0.\r\n");
    UART1_send_string("File: ");
    UART1_send_string(__FILE__);
    UART1_send_string("\r\nCompiled: ");
    UART1_send_string(__DATE__);
    UART1_send_string(", ");
    UART1_send_string(__TIME__);
    UART1_send_string("\r\n");
    return;
}

void cmd_reset(unsigned char argc, char *argv[]) reentrant {
    UART1_send_string("System Resetting...\r\n");
    RSTSRC = 0x10;
}

const cli_cmd_t code cli_commands[] = {
    {"tibo", "--help",  "Show all commands", cmd_help},
    {"tibo", "psize", "Modify the size of Window", cmd_psize},
    {"tibo", "enable", "Display CH1/CH2", cmd_enable},
    {"tibo", "disable", "Shutdown CH1/CH2 display", cmd_disable},
    {"tibo", "info",   "Show device info", cmd_info},
    {"tibo", "--reset", "Reset the System", cmd_reset},
};

const unsigned char cli_cmd_count = sizeof(cli_commands) / sizeof(cli_commands[0]);

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

    if (strcmp(argv[0], "tibo") != 0) {
        UART1_send_string("Usage: tibo <cmd> <args>\r\n");
        return;
    }

    if (argc < 2) {
        UART1_send_string("Usage: tibo <cmd> <args>\r\n");
        return;
    }

	for (i = 0; i < cli_cmd_count; i++) {
        if ((strcmp(argv[0], cli_commands[i].cli_name) == 0) &&
            (strcmp(argv[1], cli_commands[i].name) == 0))
        {
            cli_commands[i].handler(argc - 1, &argv[1]);
            return;
        }
	}
	UART1_send_string("Unknown command: ");
    UART1_send_string(argv[1]);
    UART1_send_string("\r\n");
}

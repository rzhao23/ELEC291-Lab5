#ifndef CMD_H
#define CMD_H

typedef void (*cli_handler_t)(unsigned char argc, char *argv[]) reentrant;
typedef struct {
    const char *cli_name;
    const char *name;
    const char *help;
    cli_handler_t handler;
} cli_cmd_t;

void cmd_help(unsigned char argc, char *argv[]) reentrant;
void cmd_psize(unsigned char argc, char *argv[]) reentrant;
void cmd_enable(unsigned char argc, char *argv[]) __reentrant;
void cmd_disable(unsigned char argc, char *argv[]) reentrant;
void cmd_info(unsigned char argc, char *argv[]) reentrant;
void cmd_reset(unsigned char argc, char *argv[]) reentrant;

extern const cli_cmd_t code cli_commands[];
extern const unsigned char cli_cmd_count;

void cli_parse_and_dispatch(char *input);

#endif
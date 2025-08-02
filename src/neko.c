#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#define BUFSIZE 4096
#define MAX_LINE 4096
#define LINE_DISPLAY_WIDTH 64

typedef struct {
    int number_all;
    int number_nonblank;
    int show_ends;
    int show_tabs;
    int center_left;
    int use_color;
}opt_flag_t;

opt_flag_t opt_flag = {0};

/* STDINかSTDOUTを参照して端末の横幅を取得する関数  */
int get_terminal_width(void) {
	int default_width = 80;

	// STDINの横幅を使う 
    struct winsize ws;
	if (isatty(STDIN_FILENO)) {
		if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) != -1)
			return ws.ws_col;
	}

    // STDERRの横幅を使う
	if (isatty(STDERR_FILENO)) {
		if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) != -1)
			return ws.ws_col;
	}

    return default_width;
}

/* 文字に色をつける関数  */
void print_with_color(char c) {
    if (isdigit(c)) {
        dprintf(1, "\033[1;34m%c\033[0m", c);
    } else if (isalpha(c)) {
        dprintf(1, "\033[1;32m%c\033[0m", c);
    } else {
        dprintf(1, "%c", c);
    }
}

/* 一行分の文字列を出力する関数  */
void print_line(const char *linebuf, int line_num) {
    int content_width = 0;
//    int line_len = strlen(linebuf);
    char expanded_line[MAX_LINE * 4];
    int idx = 0;

    // タブや^I，$などの展開処理
    for (int i = 0; linebuf[i] != '\0'; i++) {
        if (opt_flag.show_tabs && linebuf[i] == '\t') {
            strcpy(&expanded_line[idx], "^I");
            idx += 2;
            content_width += 2;
        } else {
            expanded_line[idx++] = linebuf[i];
            content_width++;
        }
    }

    if (opt_flag.show_ends) {
        expanded_line[idx++] = '$';
        content_width++;
    }

    expanded_line[idx] = '\0';

	int line_printed = 0;
	int offset = 0;

    int terminal_width = get_terminal_width();
    int margin = (terminal_width - LINE_DISPLAY_WIDTH) / 2;

    while (offset < idx) {
        for (int i = 0; i < margin; i++) {
            dprintf(1, " ");
        }

        if ((opt_flag.number_all 
            || (opt_flag.number_nonblank 
				&& linebuf[0] != '\n' && linebuf[0] != '\0')) 
            && line_printed == 0) {
            dprintf(1, "%6d\t", line_num);
        } else if (opt_flag.number_all || opt_flag.number_nonblank) {
            char number_buf[16];
            snprintf(number_buf, sizeof(number_buf), "%6d\t", 0);
            for (size_t i = 0; i < strlen(number_buf); i++) {
                dprintf(1, " ");
            }
        }

        // LINE_DISPLAY_WIDTH 分ずつ出力
        int remaining = idx - offset;
        int chunk = remaining < LINE_DISPLAY_WIDTH ? remaining : LINE_DISPLAY_WIDTH;
        for (int i = 0; i < chunk; i++) {
            char c = expanded_line[offset + i];
            if (opt_flag.use_color) {
                print_with_color(c);
            } else {
                dprintf(1, "%c", c);
            }
        }

        dprintf(1, "\n");

        offset += chunk;
        line_printed++;
    }
}

/* catの本体  */
int enhanced_cat(int fd, const char *filename) {
    char buf[BUFSIZE];
    char linebuf[MAX_LINE];
    ssize_t n;
    int i, line_idx = 0, line_num = 1;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (i = 0; i < n; i++) {
            char c = buf[i];
            if (line_idx < MAX_LINE - 1) {
                linebuf[line_idx++] = c;
            }

            if (c == '\n') {
                linebuf[line_idx - 1] = '\0';  // 改行を除去
                print_line(linebuf, line_num);
                if (opt_flag.number_all 
                        || (opt_flag.number_nonblank && linebuf[0] != '\0')) {
                    line_num++;
                }
                line_idx = 0;
            }
        }
    }

    // 改行で終わってない最終行
    if (line_idx > 0) {
        linebuf[line_idx] = '\0';
        print_line(linebuf, line_num);
    }

    if (n < 0) {
        perror(filename);
        return 1;
    }
    return 0;
}

/* 使い方を表示する関数 */
void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options] [file...]\n"
        "Options:\n"
        "  -n   Number all lines\n"
        "  -b   Number non-empty lines only\n"
        "  -E   Show $ at end of lines\n"
        "  -T   Show TAB characters as ^I\n"
        "  -c   Center the entire line output\n"
        "  -C   Color output by character type\n",
        prog
    );
}

int main(int argc, char *argv[]) {

    // optionの読み込み
    int opt;
    while ((opt = getopt(argc, argv, "nbETcC")) != -1) {
        switch (opt) {
            case 'n':
                opt_flag.number_all = true;
                break;
            case 'b':
                opt_flag.number_nonblank = true;
                break;
            case 'E':
                opt_flag.show_ends = true;
                break;
            case 'T':
                opt_flag.show_tabs = true;
                break;
            case 'c':
                opt_flag.center_left = true;
                break;
            case 'C': 
                opt_flag.use_color = true;
                break;
            default:
                usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // NUMBER_ALL (-n) と NUBMER_NONBLANK (-b) は共存し得ないので中断
    if (opt_flag.number_all && opt_flag.number_nonblank) {
        fprintf(stderr, "Error: -n and -b are mutually exclusive\n");
        exit(EXIT_FAILURE);
    }

    if (optind == argc) {

        // 標準入力
        enhanced_cat(STDIN_FILENO, "stdin");
    } else {
        for (int i = optind; i < argc; i++) {
            int fd = open(argv[i], O_RDONLY);
            if (fd < 0) {
                perror(argv[i]);
                continue;
            }
            enhanced_cat(fd, argv[i]);
            close(fd);
        }
    }

    return 0;
}


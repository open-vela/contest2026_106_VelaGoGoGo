/****************************************************************************
 * app/home_scense/emoji_play_main.c
 * NSH/ADB command for sending Emoji requests to home_scense.
 ****************************************************************************/

#include "home_scense_emoji_ipc.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
    int ret;

    if (argc != 2) {
        printf("Usage: emoji_play <02|03|stop>\n");
        return 1;
    }

    if (strcmp(argv[1], "stop") == 0) {
        ret = home_scense_emoji_stop();
    } else {
        ret = home_scense_emoji_play(argv[1]);
    }

    if (ret < 0) {
        printf("emoji_play: request failed: %d\n", ret);
        return 1;
    }

    printf("emoji_play: request sent: %s\n", argv[1]);
    return 0;
}

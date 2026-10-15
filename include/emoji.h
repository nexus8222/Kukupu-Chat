#ifndef EMOJI_H
#define EMOJI_H

#define EMOJI_COUNT 180
extern const char *emoji_list[EMOJI_COUNT];

void display_emoji_tab_with_index();
const char* get_emoji_by_index(int idx);

#endif

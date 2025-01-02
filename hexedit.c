#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <curses.h>
#include <stdbool.h>

#define WINFILE

#ifdef WINFILE
#include <windows.h>
#endif

#define INPUT_BUFFER_SIZE 255

typedef struct
{
	size_t size;
	size_t allocated_size;
	int block_size;
	char name[INPUT_BUFFER_SIZE + 1];
	char path[INPUT_BUFFER_SIZE + 1];
	unsigned char *data;
} file_t;

typedef struct
{
	unsigned int top_line;
	size_t cursor_index;
	int cursor_increment;
	bool ascii_edit;
	bool inserting;
	int cursor_row;
	int cursor_col;
} state_t;

void draw_screen(file_t *file, state_t *state);
void print_data_byte_hex(file_t *file, state_t *state, size_t index);
void print_data_byte_char(file_t *file, state_t *state, size_t index);
void change_byte(file_t *file, state_t *state, int key);
void move_cursor_safe(file_t *file, state_t *state, int n);
void save_file(file_t *file, FILE *f);
void goto_addr(file_t *file, state_t *state, long int addr);
void find_str(file_t *file, state_t *state, const char *str);
void find_hex(file_t *file, state_t *state, const char *str);
void append_bytes(file_t *file, state_t *state, int num_bytes);
void insert_bytes(file_t *file, state_t *state, int num_bytes);

int main(int argc, char *argv[])
{
	char inbuf[INPUT_BUFFER_SIZE] = {0};
	file_t file;
	state_t state;
	// Acquire a file to edit, either through argv or through prompting
	FILE *f = NULL;
	if(argc > 1)
	{
		f = fopen(argv[1], "ab+");
		// Copy to inbuf for grabbing the path and name
		strcpy(inbuf, argv[1]);
	}
	while(f == NULL)
	{
		#ifndef WINFILE
		
		printf("Please specify a file to open\n");
		fgets(inbuf, INPUT_BUFFER_SIZE, stdin);
		inbuf[strcspn(inbuf, "\r\n")] = 0;
		if(strlen(inbuf) == 0)
		{
			continue;
		}
		char *path_to_open = inbuf;
		// Remove quotation marks and attempt to open
		if(inbuf[strlen(inbuf) - 1] == '\"')
		{
			inbuf[strlen(inbuf) - 1] = 0;
		}
		if(inbuf[0] == '\"')
		{
			path_to_open++;
		}
		f = fopen(path_to_open, "ab+");
		
		#else
		
		OPENFILENAME ofn;       // common dialog box structure
		char szFile[260];       // buffer for file name

		// Initialize OPENFILENAME
		ZeroMemory(&ofn, sizeof(ofn));
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = NULL;
		ofn.lpstrFile = szFile;
		// Set lpstrFile[0] to '\0' so that GetOpenFileName does not 
		// use the contents of szFile to initialize itself.
		ofn.lpstrFile[0] = '\0';
		ofn.nMaxFile = sizeof(szFile);
		ofn.lpstrFilter = "All Files\0*.*\0";
		ofn.nFilterIndex = 1;
		ofn.lpstrFileTitle = NULL;
		ofn.nMaxFileTitle = 0;
		ofn.lpstrInitialDir = NULL;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

		// Display the Open dialog box. 
		if (GetOpenFileName(&ofn) == TRUE)
		{
			f = fopen(ofn.lpstrFile, "ab+");
		}
		else
		{
			endwin();
			return 0;
		}
		
		#endif
	}
	// Get file size
	fseek(f, 0, SEEK_END);
	file.size = ftell(f);
	// Get file path and name
	strcpy(file.path, inbuf);
	char *fname = strrchr(inbuf, '\\');
	if(fname == NULL)
	{
		fname = strrchr(inbuf, '/');
	}
	if(fname != NULL)
	{
		fname++;
		strcpy(file.name, fname);
	}
	else
	{
		strcpy(file.name, inbuf);
	}
	// Read the file into the data char *
	file.block_size = 1024;
	file.allocated_size = (file.size / file.block_size + 1) * file.block_size;
	file.data = malloc(file.allocated_size);
	fseek(f, 0, SEEK_SET);
	fread(file.data, sizeof(*file.data), file.size, f);
	fclose(f);
	// Initialize curses
	WINDOW *window = initscr();
	keypad(window, TRUE);
	bool running = true;
	state.top_line = 0;
	state.cursor_index = 0;
	state.ascii_edit = false;
	state.inserting = file.size == 0;
	curs_set(state.inserting ? 1 : 2);
	// Main loop
	while(running)
	{
		draw_screen(&file, &state);
		refresh();
		int key = getch();
		state.cursor_increment = state.ascii_edit ? 2 : 1;
		switch(key)
		{
			case KEY_F(1):
			save_file(&file, f);
			break;
			
			case KEY_EXIT:
			case KEY_CANCEL:
			case KEY_CLEAR:
			case KEY_BREAK:
			case KEY_RESET:
			case KEY_SRESET:
			case KEY_CLOSE:
			case KEY_SUSPEND:
			case KEY_F(2):
			running = false;
			break;
			
			case KEY_F(3):
			mvprintw(20, 0, "Address to go to: 0x");
			getnstr(inbuf, INPUT_BUFFER_SIZE);
			goto_addr(&file, &state, strtol(inbuf, NULL, 16));
			break;
			
			case KEY_F(4):
			mvprintw(20, 0, "String to find: ");
			getnstr(inbuf, INPUT_BUFFER_SIZE);
			find_str(&file, &state, inbuf);
			break;
			
			case KEY_F(5):
			mvprintw(20, 0, "Hex string to find: ");
			getnstr(inbuf, INPUT_BUFFER_SIZE);
			find_hex(&file, &state, inbuf);
			break;
			
			case KEY_F(6):
			mvprintw(20, 0, "Number of bytes to append: ");
			getnstr(inbuf, INPUT_BUFFER_SIZE);
			append_bytes(&file, &state, atoi(inbuf));
			break;
			
			case KEY_UP:
			move_cursor_safe(&file, &state, -0x20);
			break;
			
			case KEY_DOWN:
			move_cursor_safe(&file, &state, 0x20);
			break;
			
			case KEY_LEFT:
			move_cursor_safe(&file, &state, -state.cursor_increment);
			break;
			
			case KEY_RIGHT:
			move_cursor_safe(&file, &state, state.cursor_increment);
			break;
			
			case KEY_PPAGE: // Page up
			move_cursor_safe(&file, &state, -0x200);
			break;
			
			case KEY_NPAGE: // Page down
			move_cursor_safe(&file, &state, 0x200);
			break;
			
			case KEY_HOME: // Home
			state.cursor_index &= ~0x1F;
			break;
			
			case KEY_SHOME: // Shift + Home
			state.cursor_index = 0;
			break;
			
			case KEY_END: // End
			goto_addr(&file, &state, (state.cursor_index / 2) | 0xF);
			break;
			
			case KEY_SEND: // Shift + End
			state.cursor_index = (file.size - !state.inserting) * 2;
			break;
			
			case '\t': // Tab
			state.ascii_edit ^= 1;
			state.cursor_index &= ~1;
			break;
			
			case 0x08: // Backspace
			move_cursor_safe(&file, &state, -state.cursor_increment);
			break;
			
			case KEY_IC: // Insert
			state.inserting ^= 1;
			curs_set(state.inserting ? 1 : 2);
			move_cursor_safe(&file, &state, 0);
			break;
			
			case KEY_DC: // Delete
			insert_bytes(&file, &state, -1);
			break;
			
			default:
			change_byte(&file, &state, key);
			move_cursor_safe(&file, &state, state.cursor_increment);
			break;
		}
		// Scroll the screen as necessary
		if(state.cursor_index < state.top_line * 0x20)
		{
			state.top_line = state.cursor_index / 0x20;
		}
		if(state.cursor_index >= (state.top_line + 0x10) * 0x20)
		{
			state.top_line = state.cursor_index / 0x20 - 0xF;
		}
	}
	// Close program
	endwin();
	free(file.data);
	return 0;
}

// Renders the editing window and all other information to the terminal
void draw_screen(file_t *file, state_t *state)
{
	int i, j;
	clear();
	move(0, 0);
	char title_bar[100];
	sprintf(title_bar, "%-24.24s    %d B (0x%x B)    0x%x B Allocated", file->name, file->size, file->size, file->allocated_size);
	attron(A_REVERSE);
	printw("%-78.78s\n", title_bar);
	attroff(A_REVERSE);
	for(i = 0; i < 0x10; i++)
	{
		printw("%08x: ", (state->top_line + i) * 0x10);
		for(j = 0; j < 0x08; j++)
		{
			print_data_byte_hex(file, state, (state->top_line + i) * 0x20 + j * 2);
		}
		printw(" ");
		for(; j < 0x10; j++)
		{
			print_data_byte_hex(file, state, (state->top_line + i) * 0x20 + j * 2);
		}
		printw("  ");
		for(j = 0; j < 0x10; j++)
		{
			print_data_byte_char(file, state, (state->top_line + i) * 0x20 + j * 2);
		}
		printw("\n");
	}
	printw("<F1> Save    <F2> Quit    <F3> Goto    <F4> Find String    <F5> Find Hex\n");
	printw("<F6> Append    <TAB> Toggle Mode\n");
	if(state->ascii_edit)
	{
		int cursor_x = (state->cursor_index - state->top_line * 0x20) % 0x20;
		int cursor_y = (state->cursor_index - state->top_line * 0x20) / 0x20;
		move(1 + cursor_y, 61 + cursor_x / 2);
	}
	else
	{
		int cursor_x = (state->cursor_index - state->top_line * 0x20) % 0x20;
		int cursor_y = (state->cursor_index - state->top_line * 0x20) / 0x20;
		move(1 + cursor_y, 12 + cursor_x / 2 * 3 + (state->cursor_index & 1) - (cursor_x < 0x10));
	}
}

// Prints a correctly formatted and highlighted hex byte in the editing window
void print_data_byte_hex(file_t *file, state_t *state, size_t index)
{
	bool high_nibble = !(state->cursor_index & 1);
	if(index >= file->size * 2)
	{
		printw("   ");
	}
	else
	{
		printw(" ");
		if(index / 2 == state->cursor_index / 2 && (high_nibble || state->ascii_edit) && state->ascii_edit)
		{
			attron(A_REVERSE);
		}
		printw("%01x", file->data[index / 2] >> 4);
		if(index / 2 == state->cursor_index / 2 && (!high_nibble || state->ascii_edit) && state->ascii_edit)
		{
			attron(A_REVERSE);
		}
		else
		{
			attroff(A_REVERSE);
		}
		printw("%01x", file->data[index / 2] & 0xF);
		attroff(A_REVERSE);
	}
}

// Prints a correctly formatted and highlighted character in the editing window
void print_data_byte_char(file_t *file, state_t *state, size_t index)
{
	if(index >= file->size * 2)
	{
		printw(" ");
	}
	else
	{
		if(index / 2 == state->cursor_index / 2 && !state->ascii_edit)
		{
			attron(A_REVERSE);
		}
		if(isprint(file->data[index / 2]))
		{
			printw("%c", file->data[index / 2]);
		}
		else
		{
			printw(".");
		}
		attroff(A_REVERSE);
	}
}

// Modifies a nibble or a full byte, inserting if asked
void change_byte(file_t *file, state_t *state, int key)
{
	const char *hex_digits = "0123456789abcdef";
	bool high_nibble = !(state->cursor_index & 1);
	if(state->ascii_edit)
	{
		if(state->inserting)
		{
			insert_bytes(file, state, 1);
		}
		file->data[state->cursor_index / 2] = key;
	}
	else
	{
		if(!isxdigit(key))
		{
			return;
		}
		if(high_nibble && state->inserting)
		{
			insert_bytes(file, state, 1);
		}
		file->data[state->cursor_index / 2] &= ~(0xF << (high_nibble * 4));
		int nibble = (int) (strchr(hex_digits, (char) tolower(key)) - hex_digits);
		file->data[state->cursor_index / 2] |= nibble << (high_nibble * 4);
	}
}

// Moves the editing cursor with bounds checking
void move_cursor_safe(file_t *file, state_t *state, int n)
{
	if((int) state->cursor_index + n < 0 || file->size == 0)
	{
		state->cursor_index = 0;
		return;
	}
	if(state->cursor_index + n >= file->size * 2 + state->inserting * state->cursor_increment)
	{
		state->cursor_index = file->size * 2 + state->inserting * state->cursor_increment - state->cursor_increment;
		return;
	}
	state->cursor_index += n;
}

// Writes file buffer contents back to the original file
void save_file(file_t *file, FILE *f)
{
	f = fopen(file->path, "wb");
	if(f == NULL)
	{
		mvaddstr(1, 0, "FAILED TO SAVE FILE!");
		getch();
		return;
	}
	fwrite(file->data, sizeof(*file->data), file->size, f);
	fclose(f);
}

// Moves the editing cursor to an address with bounds checking
void goto_addr(file_t *file, state_t *state, long int addr)
{
	if(addr < 0)
	{
		state->cursor_index = 0;
	}
	else if(addr >= file->size + state->inserting)
	{
		state->cursor_index = file->size * 2 - 1 + state->inserting;
	}
	else
	{
		state->cursor_index = addr * 2;
	}
}

// Finds a specified string after the editing cursor and moves the cursor to that string
void find_str(file_t *file, state_t *state, const char *str)
{
	int i, j;
	int len = strlen(str);
	j = 0;
	for(i = state->cursor_index / 2 + 1; i < file->size; i++)
	{
		if(file->data[i] == str[j])
		{
			j++;
		}
		else
		{
			j = 0;
		}
		if(j == len)
		{
			state->cursor_index = (i - j + 1) * 2;
			break;
		}
	}
}

// Finds a specified hex string after the editing cursor and moves the cursor to that string
void find_hex(file_t *file, state_t *state, const char *str)
{
	int i, j;
	int len = strlen(str) / 2;
	long *tofind = malloc(len * sizeof(long));
	char hexbuf[3] = {0};
	for(i = 0; i < len; i++)
	{
		hexbuf[0] = str[i * 2];
		hexbuf[1] = str[i * 2 + 1];
		tofind[i] = strtol(hexbuf, NULL, 16);
	}
	j = 0;
	for(i = state->cursor_index / 2 + 1; i < file->size; i++)
	{
		if(file->data[i] == tofind[j])
		{
			j++;
		}
		else
		{
			j = 0;
		}
		if(j == len)
		{
			state->cursor_index = (i - j + 1) * 2;
			break;
		}
	}
	free(tofind);
}

// Appends the specified number of bytes to the file buffer, reallocating memory if needed
void append_bytes(file_t *file, state_t *state, int num_bytes)
{
	if(file->size + num_bytes > file->allocated_size)
	{
		file->allocated_size = ((file->size + num_bytes) / file->block_size + 1) * file->block_size;
		file->data = realloc(file->data, file->allocated_size);
	}
	memset(&(file->data[file->size]), 0, num_bytes);
	file->size += num_bytes;
}

// Inserts the specified number of bytes after the editing cursor to the file buffer, reallocating memory if needed
void insert_bytes(file_t *file, state_t *state, int num_bytes)
{
	int deleting = num_bytes < 0;
	void *dest = &(file->data[state->cursor_index / 2 + num_bytes + deleting]);
	void *src = &(file->data[state->cursor_index / 2 + deleting]);
	if((int) file->size + num_bytes < 0)
	{
		return;
	}
	if(file->size + num_bytes > file->allocated_size)
	{
		file->allocated_size = ((file->size + num_bytes) / file->block_size + 1) * file->block_size;
		file->data = realloc(file->data, file->allocated_size);
	}
	memmove(dest, src, file->size - (state->cursor_index / 2 + num_bytes - 1));
	if(num_bytes > 0)
	{
		memset(&(file->data[state->cursor_index / 2]), 0, num_bytes);
	}
	file->size += num_bytes;
	move_cursor_safe(file, state, 0);
}

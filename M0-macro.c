/* Copyright (C) 2016 Jeremiah Orians
 * This file is part of stage0.
 *
 * stage0 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * stage0 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with stage0.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#define max_string 63

FILE* source_file;
bool Reached_EOF;
int BigEndian;

struct Token
{
	struct Token* next;
	uint8_t type;
	char* Text;
	char* Expression;
};

enum type
{
	macro = 1,
	str = (1 << 1)
};

struct Token* newToken()
{
	struct Token* p;

	p = calloc (1, sizeof (struct Token));
	if (NULL == p)
	{
		fprintf (stderr, "calloc failed.\n");
		exit (EXIT_FAILURE);
	}

	return p;
}

struct Token* addToken(struct Token* head, struct Token* p)
{
	if(NULL == head)
	{
		return p;
	}

	for(struct Token* i = head; NULL != i; i = i->next)
	{
		if(NULL == i->next)
		{
			i->next = p;
			return head;
		}
	}
	return head;
}

void purge_lineComment()
{
	int c = fgetc(source_file);
	while((10 != c) && (13 != c))
	{
		c = fgetc(source_file);
	}
}

char* store_atom(char c)
{
	char* store = calloc(max_string + 1, sizeof(char));
	if(NULL == store)
	{
		fprintf(stderr, "Exhusted available memory\n");
		exit(EXIT_FAILURE);
	}
	int32_t ch;
	uint32_t i = 0;
	ch = c;
	do
	{
		store[i] = (char)ch;
		ch = fgetc(source_file);
		i = i + 1;
	} while ((9 != ch) && (10 != ch) && (32 != ch) && (i <= max_string));

	return store;
}

char* store_string(char c)
{
	char* store = calloc(max_string + 1, sizeof(char));
	if(NULL == store)
	{
		fprintf(stderr, "Exhusted available memory\n");
		exit(EXIT_FAILURE);
	}
	int32_t ch;
	uint32_t i = 0;
	ch = c;
	do
	{
		store[i] = (char)ch;
		i = i + 1;
		ch = fgetc(source_file);
		if(-1 == ch)
		{
			fprintf(stderr, "Unmatched \"!\n");
			exit(EXIT_FAILURE);
		}
		if(max_string == i)
		{
			fprintf(stderr, "String: %s exceeds max string size\n", store);
			exit(EXIT_FAILURE);
		}
	} while(ch != c);

	return store;
}

struct Token* Tokenize_Line(struct Token* head)
{
	int32_t c;

restart:
	c = fgetc(source_file);

	if((35 == c) || (59 == c))
	{
		purge_lineComment();
		goto restart;
	}

	if((9 == c) || (10 == c) || (32 == c))
	{
		goto restart;
	}

	struct Token* p = newToken();
	if(-1 == c)
	{
		Reached_EOF = true;
		free(p);
		return head;
	}
	else if((34 == c) || (39 == c))
	{
		p->Text = store_string(c);
		p->type = str;
	}
	else
	{
		p->Text = store_atom(c);
	}

	return addToken(head, p);
}

void setExpression(struct Token* p, char match[], char Exp[])
{
	for(struct Token* i = p; NULL != i; i = i->next)
	{
		/* Leave macros alone */
		if((i->type & macro))
		{
			continue;
		}
		else if(0 == strncmp(i->Text, match, max_string))
		{ /* Only if there is an exact match replace */
			i->Expression = Exp;
		}
	}
}

void identify_macros(struct Token* p)
{
	if(0 == strncmp(p->Text, "DEFINE", max_string))
	{
		p->type = macro;
		p->Text = p->next->Text;
		if(p->next->next->type & str)
		{
			p->Expression = p->next->next->Text + 1;
		}
		else
		{
			p->Expression = p->next->next->Text;
		}
		p->next = p->next->next->next;
	}

	if(NULL != p->next)
	{
		identify_macros(p->next);
	}
}

void line_macro(struct Token* p)
{
	for(struct Token* i = p; NULL != i; i = i->next)
	{
		if(i->type & macro)
		{
			setExpression(i->next, i->Text, i->Expression);
		}
	}
}

void hexify_string(struct Token* p)
{
	char table[16] = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46};
	int i = ((strnlen(p->Text + 1 , max_string)/4) + 1) * 8;

	char* d = calloc(max_string, sizeof(char));
	p->Expression = d;

	while(0 < i)
	{
		i = i - 1;
		d[i] = 0x30;
	}

	while( i < max_string)
	{
		if(0 == p->Text[i+1])
		{
			i = max_string;
		}
		else
		{
			d[2*i]  = table[p->Text[i+1] / 16];
			d[2*i + 1] = table[p->Text[i+1] % 16];
			i = i + 1;
		}
	}
}

void process_string(struct Token* p)
{
	if(p->type & str)
	{
		if('\'' == p->Text[0])
		{
			p->Expression = p->Text + 1;
		}
		else if('"' == p->Text[0])
		{
			hexify_string(p);
		}
	}

	if(NULL != p->next)
	{
		process_string(p->next);
	}
}


void preserve_other(struct Token* p)
{
	if(NULL != p->next)
	{
		preserve_other(p->next);
	}

	if((NULL == p->Expression) && !(p->type & macro))
	{
		p->Expression = p->Text;
	}
}

int32_t numerate_string(char a[])
{
	char *ptr;
	return (uint32_t)strtol(a, &ptr, 0);
}

char* LittleEndian(uint32_t value, char* c, int Number_of_bytes)
{
	/* {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'} */
	char table[16] = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46};

	switch(Number_of_bytes)
	{
		case 4:
		{
			c[6] = table[value >> 28];
			c[7] = table[(value >> 24)% 16];
		}
		case 3:
		{
			c[4] = table[(value >> 20)% 16];
			c[5] = table[(value >> 16)% 16];
		}
		case 2:
		{
			c[2] = table[(value >> 12)% 16];
			c[3] = table[(value >> 8)% 16];
		}
		case 1:
		{
			c[0] = table[(value >> 4)% 16];
			c[1] = table[value % 16];
			break;
		}
		default:
		{
			fprintf(stderr, "Recieved invalid number of bytes in LittleEndian %d\n", Number_of_bytes);
			exit(EXIT_FAILURE);
		}
	}
	return c;
}

char* express_number(int32_t value, char c)
{
	char* ch;
	if('!' == c)
	{
		ch = calloc(3, sizeof(char));
		if(BigEndian)
		{
			sprintf(ch, "%02x", value);
		}
		else
		{
			ch = LittleEndian(value, ch, 1);
		}
	}
	else if('@' == c)
	{
		ch = calloc(5, sizeof(char));
		if(BigEndian)
		{
			sprintf(ch, "%04x", value);
		}
		else
		{
			ch = LittleEndian(value, ch, 2);
		}

	}
	else if('%' == c)
	{
		ch = calloc(9, sizeof(char));
		if(BigEndian)
		{
			sprintf(ch, "%08x", value);
		}
		else
		{
			ch = LittleEndian(value, ch, 4);
		}

	}
	else
	{
		fprintf(stderr, "Given symbol %c to express immediate value %d\n", c, value);
		exit(EXIT_FAILURE);
	}
	return ch;
}

void eval_immediates(struct Token* p)
{
	if(NULL != p->next)
	{
		eval_immediates(p->next);
	}

	if((NULL == p->Expression) && !(p->type & macro))
	{
		int32_t value;
		value = numerate_string(p->Text + 1);

		if(('0' == p->Text[1]) || (0 != value))
		{
			p->Expression = express_number(value, p->Text[0]);
		}
	}
}

void print_hex(struct Token* p)
{
	if(p->type ^ macro)
	{
		fprintf(stdout, "\n%s", p->Expression);
	}

	if(NULL != p->next)
	{
		print_hex(p->next);
	}
	else
	{
		fprintf(stdout, "\n");
	}
}

/* Standard C main program */
int main(int argc, char **argv)
{
/* Default endianness is that of the native host */
#if defined(__BYTE_ORDER) && __BYTE_ORDER == __BIG_ENDIAN || \
	defined(__BIG_ENDIAN__) || \
	defined(__ARMEB__) || \
	defined(__THUMBEB__) || \
	defined(__AARCH64EB__) || \
	defined(_MIBSEB) || defined(__MIBSEB) || defined(__MIBSEB__)
// It's a big-endian target architecture
		BigEndian = true;
#elif defined(__BYTE_ORDER) && __BYTE_ORDER == __LITTLE_ENDIAN || \
	defined(__LITTLE_ENDIAN__) || \
	defined(__ARMEL__) || \
	defined(__THUMBEL__) || \
	defined(__AARCH64EL__) || \
	defined(_MIPSEL) || defined(__MIPSEL) || defined(__MIPSEL__)
// It's a little-endian target architecture
	BigEndian = false;
#else
#error "I don't know what architecture this is!"
	exit(EXIT_FAILURE);
#endif

	struct Token* head = NULL;
	int c;
	static struct option long_options[] = {
		{"BigEndian", no_argument, &BigEndian, true},
		{"LittleEndian", no_argument, &BigEndian, false},
		{"file", required_argument, 0, 'f'},
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'v'},
		{0, 0, 0, 0}
	};

	int option_index = 0;
	while ((c = getopt_long(argc, argv, "f:h", long_options, &option_index)) != -1)
	{
		switch(c)
		{
			case 0: break;
			case 'h':
			{
				fprintf(stderr, "Usage: %s -f FILENAME1 {-f FILENAME2} (--BigEndian|--LittleEndian) [--BaseAddress 12345] [--Architecture 12345]\n", argv[0]);
				fprintf(stderr, "Architecture 0: Knight; 1: x86; 2: AMD64");
				exit(EXIT_SUCCESS);
			}
			case 'f':
			{
				source_file = fopen(optarg, "r");
				Reached_EOF = false;
				while(!Reached_EOF)
				{
					head = Tokenize_Line(head);
				}
				break;
			}
			case 'v':
			{
				fprintf(stdout, "M0 0.1\n");
			}
			default:
			{
				fprintf(stderr, "Unknown option\n");
				exit(EXIT_FAILURE);
			}
		}
	}

	if(NULL == head)
	{
		fprintf(stderr, "Either no input files were given or they were empty\n");
		exit(EXIT_FAILURE);
	}

	identify_macros(head);
	line_macro(head);
	process_string(head);
	eval_immediates(head);
	preserve_other(head);
	print_hex(head);

	return EXIT_SUCCESS;
}

#include <stdint.h>
#include <SDL.h>
#include "joypad.h"
#include "interrupt.h"

static struct
{
#define JOYPAD_KEY_A      0x01
#define JOYPAD_KEY_B      0x02
#define JOYPAD_KEY_UP     0x04
#define JOYPAD_KEY_DOWN   0x08
#define JOYPAD_KEY_LEFT   0x10
#define JOYPAD_KEY_RIGHT  0x20
#define JOYPAD_KEY_START  0x40
#define JOYPAD_KEY_SELECT 0x80
	uint32_t host_keys;

#define REGISTER_DIRECTION_KEYS 0x10
#define REGISTER_BUTTON_KEYS	0x20
#define REGISTER_START_OR_DOWN  0x08
#define REGISTER_SELECT_OR_UP	0x04
#define REGISTER_B_OR_LEFT	0x02
#define REGISTER_A_OR_RIGHT	0x01
	uint8_t gb_register;
} joypad;

int32_t joypad_init(void)
{
	memset(&joypad, 0, sizeof(joypad));
	return 0;
}

void joypad_set(uint8_t value)
{
	joypad.gb_register = value;
}

uint8_t joypad_get(void)
{
	uint8_t value;

	value = joypad.gb_register | (REGISTER_A_OR_RIGHT | REGISTER_B_OR_LEFT | REGISTER_SELECT_OR_UP | REGISTER_START_OR_DOWN);

	if ( (joypad.gb_register & REGISTER_DIRECTION_KEYS) == 0 )
	{
		if ( (joypad.host_keys & JOYPAD_KEY_DOWN) == JOYPAD_KEY_DOWN )
			value &= ~REGISTER_START_OR_DOWN;
		if ( (joypad.host_keys & JOYPAD_KEY_UP) == JOYPAD_KEY_UP )
			value &= ~REGISTER_SELECT_OR_UP;
		if ( (joypad.host_keys & JOYPAD_KEY_LEFT) == JOYPAD_KEY_LEFT )
			value &= ~REGISTER_B_OR_LEFT;
		if ( (joypad.host_keys & JOYPAD_KEY_RIGHT) == JOYPAD_KEY_RIGHT )
			value &= ~REGISTER_A_OR_RIGHT;
	}
	if ( (joypad.gb_register & REGISTER_BUTTON_KEYS) == 0 )
	{
		if ( (joypad.host_keys & JOYPAD_KEY_START) == JOYPAD_KEY_START )
			value &= ~REGISTER_START_OR_DOWN;
		if ( (joypad.host_keys & JOYPAD_KEY_SELECT) == JOYPAD_KEY_SELECT )
			value &= ~REGISTER_SELECT_OR_UP;
		if ( (joypad.host_keys & JOYPAD_KEY_B) == JOYPAD_KEY_B )
			value &= ~REGISTER_B_OR_LEFT;
		if ( (joypad.host_keys & JOYPAD_KEY_A) == JOYPAD_KEY_A )
			value &= ~REGISTER_A_OR_RIGHT;
	}

	return value;
}

uint32_t joypad_handle_key(uint16_t sdlkey, uint32_t keydown)
{
	uint32_t key;
	switch ( sdlkey )
	{
		case SDLK_a:
		key = JOYPAD_KEY_A;
		break;

		case SDLK_z:
		key = JOYPAD_KEY_B;
		break;

		case SDLK_UP:
		key = JOYPAD_KEY_UP;
		break;

		case SDLK_DOWN:
		key = JOYPAD_KEY_DOWN;
		break;

		case SDLK_LEFT:
		key = JOYPAD_KEY_LEFT;
		break;

		case SDLK_RIGHT:
		key = JOYPAD_KEY_RIGHT;
		break;

		case SDLK_RETURN:
		key = JOYPAD_KEY_START;
		break;

		case SDLK_BACKSPACE:
		key = JOYPAD_KEY_SELECT;
		break;

		default:
		return 0;
	}

	if ( keydown )
		joypad.host_keys |= key;
	else
		joypad.host_keys &= ~key;

	interrupt_request(INTERRUPT_JOYPAD);

	return 1;
}

int32_t joypad_dump(FILE *file)
{
	if ( fwrite(&joypad, 1, sizeof(joypad), file) != sizeof(joypad) )
		return -1;
	return 0;
}

int32_t joypad_restore(FILE *file)
{
	if ( fread(&joypad, 1, sizeof(joypad), file) != sizeof(joypad) )
		return -1;
	return 0;
}

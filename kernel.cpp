#include "gdt.h"

// Write our own print function as we do not have IO header files in our new OS space.
void print(char* s)
{
	// every (second)alternate byte from memory address 0xb8000, when anything written into those bytes are displayed on the screen. Every other alternate memory bytes are used to specify color(4 bits for foreground and 4 bits for background color, default is set to white text on black background).
	// TYPE* ptr = (TYPE *)mem_address => ptr points to a mem location starting from mem_address and sizeof sizeof(TYPE)
	// As we need alternate blocks, we use unsigned short(u16_t) which is 2 bytes instead of 4 incase of unsigned int(u32_t). screen+i by 2*i bytes incase of u16_t, which is 4*i bytes incase of u32_t. 
	// As we need only 2nd byte of every 2 bytes of u16_t, we 'and' the mem location with 2 bytes with 0xFF00 and 'or' with 1 byte of character to be written.  
	static u16_t* screen = (u16_t *)0xb8000;
	for(int i = 0; s[i] != '\0'; i++)
		*(screen + i) = (*(screen + i) & 0xFF00) | s[i];
}

// extern "C" makes a function-name in C++ have C compilation to form a obj(.o) file (compiler does not mangle the name). As kernel_main is accessed in loader.s as kernel_main itself and as g++(C++) compiler changes name of functions while linking, we need to make use of C compilation method for this function.

typedef void (*ctor)();
// All ctors of global objects are stored between start_ctor and end_ctor (defined in linker.ld's .data section)
extern "C" ctor start_ctor;
extern "C" ctor end_ctor;
// invoke all the ctors of global objects stored between start_ctor and end_ctor manually. 
extern "C" void call_ctors()
{
	for(ctor i = start_ctor;i != end_ctor;i++)
	{
		(*i)();
	}
}

// functions paramters are from ax and bx registers pushed in loader.s
extern "C" void kernel_main(void* multiboot_structure, unsigned int magic_number)  
{
	print("Yay! You made it!");

	GlobalDescriptorTable gdt;

	// infinite loop as kernel should be running at all times	
	while(1);
}

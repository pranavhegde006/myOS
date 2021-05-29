#include <hardware_communication/interrupts.h>

using namespace myOS::common;
using namespace myOS::hardware_communication;

// It is defined in kernel.cpp, not in any other header file, so we declare it first
void print(char* s);
void print_hex(u32_t key);

InterruptManager::GateDescriptor InterruptManager::InterruptDescriptorTable[256];

// static members have to compulsory initialized and we say that there are no active interrupt manager in the beginning
InterruptManager* InterruptManager::ActiveInterruptManager = 0;

u32_t InterruptManager::interrupt_handler_main(u8_t interrupt_id, u32_t esp)
{
    // if we have a interrupt routine defined, we use that otherwise notify a print message only if interrupt is not by timer(i.e 0x20)
    if(handlers[interrupt_id] != 0)
    {
        esp = handlers[interrupt_id]->interrupt_handler(esp);
    }
    else if(interrupt_id!=0x20) // 0x20 - timer interrupt, we need no notification for that timer interrupt 
    {
        // print the message along with specific interrupt number
        print("Unhandled interrupt => ");
        print_hex(interrupt_id);
    }

    // Acknowledge the PICs, if this interrupt was generated by PICs.
	// i.e. if this was a hardware interrupt.
    // 0x20 <= interrupt_id <= 0x30 - hardware interrupts i.e from devices, otherwise no job here
    if(0x20 <= interrupt_id && interrupt_id < 0x30) // or <0x30? instead of 0x2f?
    {
        // Send ack to master PIC.
		// We always acknowledge the master PIC.
        pic_master_command.write(0x20);
        if(0x28 <= interrupt_id)
        {
            // Additionaly, if the interrupt served
            // was originally generated by the slave PIC
            // then we also need to send an ack to the slave.
            // FYI - slave interrupts start from 0x28 and go upto
            // 0x2f
            pic_slave_command.write(0x20);
        }


    }

    return esp;
}

u32_t InterruptManager::interrupt_handler(u8_t interrupt_id, u32_t esp)
{
    if(ActiveInterruptManager != 0)
    {
        // the member function(the wrapper function is static)
        return ActiveInterruptManager->interrupt_handler_main(interrupt_id, esp);
    }
    return esp;
}

void InterruptManager::set_interrupt_descriptor_table_entry(u8_t interrupt_id, u16_t code_segment_selector_offset, void (* interrupt_handler)(), u8_t descriptor_previlige_level, u8_t descriptor_type)
{
    const u8_t IDT_DESC_PRESENT = 0x80;

    // address of pointer to code segment (relative to global descriptor table)
    // and address of the handler (relative to segment)
    InterruptDescriptorTable[interrupt_id].handler_address_low_bits = ((u32_t)interrupt_handler) & 0xFFFF;
    InterruptDescriptorTable[interrupt_id].handler_address_high_bits = ((u32_t)interrupt_handler >> 16) & 0xFFFF;
    InterruptDescriptorTable[interrupt_id].gdt_code_segment_selector = code_segment_selector_offset;
    InterruptDescriptorTable[interrupt_id].access = IDT_DESC_PRESENT | descriptor_type | ((descriptor_previlige_level & 0x3) << 5);
    InterruptDescriptorTable[interrupt_id].reserved = 0;
}

InterruptManager::InterruptManager(GlobalDescriptorTable* gdt)
: pic_master_command(0x20),
pic_master_data(0x21),
pic_slave_command(0xA0),
pic_slave_data(0xA1)
{
    u16_t code_segment = gdt->compute_offset_codeSegmentSelector();
    const u8_t IDT_INTERRUPT_GATE = 0xE;

    // initialize all the IDT entries to interrupt_ignore()
    for(u16_t i = 0; i<256; i++)
    {
        handlers[i] = 0;
        set_interrupt_descriptor_table_entry(i, code_segment, &ignore_interrupt_request, 0, IDT_INTERRUPT_GATE);
    }

    // Update the entries we want to handle
    // 0x00 - timer ; pos = IRQ_BASE+0x00 = 0x20 + 0x00 = 0x20 ; IRQ_BASE is defined as 0x20 in interruptstub.s
    set_interrupt_descriptor_table_entry(0x20, code_segment, &handle_interrupt_request_0x00, 0, IDT_INTERRUPT_GATE);
    // 0x01 - keyboard ; pos = IRQ_BASE+0x01 = 0x20 + 0x01 = 0x21
    set_interrupt_descriptor_table_entry(0x21, code_segment, &handle_interrupt_request_0x01, 0, IDT_INTERRUPT_GATE);
    // 0x01 - mouse ; pos = IRQ_BASE+0x01 = 0x20 + 0x02 = 0x22
    set_interrupt_descriptor_table_entry(0x2C, code_segment, &handle_interrupt_request_0x0C, 0, IDT_INTERRUPT_GATE);



	// The CPU internally uses interrupt 0-31 for Exceptions
	// So in order to avoid that we are offsetting the master
	// and slave PICs by 32 (0x20) and 40 (0x28) respectively. Every PIC has
	// 8 interrupts so the master is from 32-39, and the slave
	// will fire interrupts from 40-47.


    // command to activate master and slave PICs
    pic_master_command.write(0x11);
    pic_slave_command.write(0x11);

    pic_master_data.write(0x20);
    pic_slave_data.write(0x28);

    pic_master_data.write(0x04);
    pic_slave_data.write(0x02);

    pic_master_data.write(0x01);
    pic_slave_data.write(0x01);

    pic_master_data.write(0x00);
    pic_slave_data.write(0x00);

    interruptDescriptorTablePointer idt;
    idt.size = 256*sizeof(GateDescriptor) - 1;
    idt.base = (u32_t)InterruptDescriptorTable;
    // load interrupt descriptor table - lidt
    asm volatile("lidt %0" : : "m"(idt));
}

InterruptManager::~InterruptManager() 
{
    deactivate();
}

void InterruptManager::activate()
{
    // make sure only one interrupt active at a time
    // if an interrupt is running, deactivate it and activate the current one
    if(ActiveInterruptManager!=0)
        ActiveInterruptManager->deactivate();
    ActiveInterruptManager = this;
    // sti - start interrupts
    asm("sti");
}

void InterruptManager::deactivate()
{
    if(ActiveInterruptManager == this)
    {
        ActiveInterruptManager = 0;
        asm("cli");       
    }
}

InterruptHandler::InterruptHandler(u8_t interrupt_id, InterruptManager* interruptManager)
{
    this->interrupt_id = interrupt_id;
    this->interruptManager = interruptManager;
    interruptManager->handlers[interrupt_id] = this;

}

InterruptHandler::~InterruptHandler()
{
    if(interruptManager->handlers[interrupt_id] == this)
    {
        interruptManager->handlers[interrupt_id] = 0;
    }
}
u32_t InterruptHandler::interrupt_handler(u32_t esp)
{
    return esp;
}
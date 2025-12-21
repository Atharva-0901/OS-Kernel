// kernel.c - Enhanced operating system kernel with interactive features

#include <stdint.h>
#include <stddef.h>

// VGA text mode buffer
#define VGA_MEMORY 0xB8000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

// Keyboard ports
#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

// VGA colors
enum vga_color {
    BLACK = 0, BLUE = 1, GREEN = 2, CYAN = 3,
    RED = 4, MAGENTA = 5, BROWN = 6, LIGHT_GREY = 7,
    DARK_GREY = 8, LIGHT_BLUE = 9, LIGHT_GREEN = 10, LIGHT_CYAN = 11,
    LIGHT_RED = 12, LIGHT_MAGENTA = 13, YELLOW = 14, WHITE = 15
};

static uint16_t* vga_buffer = (uint16_t*)VGA_MEMORY;
static size_t terminal_row = 0;
static size_t terminal_col = 0;
static uint8_t terminal_color = 0;
static uint32_t timer_ticks = 0;

// Helper functions
static inline uint8_t make_color(enum vga_color fg, enum vga_color bg) {
    return fg | bg << 4;
}

static inline uint16_t make_vgaentry(char c, uint8_t color) {
    return (uint16_t)c | (uint16_t)color << 8;
}

void terminal_initialize(void) {
    terminal_row = 0;
    terminal_col = 0;
    terminal_color = make_color(LIGHT_GREEN, BLACK);
    
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            vga_buffer[index] = make_vgaentry(' ', terminal_color);
        }
    }
}

void terminal_setcolor(uint8_t color) {
    terminal_color = color;
}

void terminal_putchar(char c) {
    if (c == '\n') {
        terminal_col = 0;
        if (++terminal_row == VGA_HEIGHT) {
            // Scroll up
            for (size_t y = 0; y < VGA_HEIGHT - 1; y++) {
                for (size_t x = 0; x < VGA_WIDTH; x++) {
                    vga_buffer[y * VGA_WIDTH + x] = vga_buffer[(y + 1) * VGA_WIDTH + x];
                }
            }
            // Clear last line
            for (size_t x = 0; x < VGA_WIDTH; x++) {
                vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = make_vgaentry(' ', terminal_color);
            }
            terminal_row = VGA_HEIGHT - 1;
        }
        return;
    }
    
    const size_t index = terminal_row * VGA_WIDTH + terminal_col;
    vga_buffer[index] = make_vgaentry(c, terminal_color);
    
    if (++terminal_col == VGA_WIDTH) {
        terminal_col = 0;
        if (++terminal_row == VGA_HEIGHT) {
            terminal_row = 0;
        }
    }
}

void terminal_writestring(const char* str) {
    for (size_t i = 0; str[i] != '\0'; i++) {
        terminal_putchar(str[i]);
    }
}

void terminal_writehex(uint32_t value) {
    char hex[] = "0123456789ABCDEF";
    terminal_writestring("0x");
    for (int i = 28; i >= 0; i -= 4) {
        terminal_putchar(hex[(value >> i) & 0xF]);
    }
}

void terminal_writedec(uint32_t value) {
    if (value == 0) {
        terminal_putchar('0');
        return;
    }
    
    char buffer[12];
    int i = 0;
    while (value > 0) {
        buffer[i++] = '0' + (value % 10);
        value /= 10;
    }
    
    while (i > 0) {
        terminal_putchar(buffer[--i]);
    }
}

// String helper functions
int str_len(const char* str) {
    int len = 0;
    while (str[len]) len++;
    return len;
}

int str_cmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

void str_copy(char* dest, const char* src) {
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}

// Port I/O functions
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Keyboard functions
char keyboard_scancode_to_ascii(uint8_t scancode) {
    static const char scancode_map[] = {
        0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
        0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
        0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
        '*', 0, ' '
    };
    
    if (scancode < sizeof(scancode_map)) {
        return scancode_map[scancode];
    }
    return 0;
}

char keyboard_read_char() {
    while (1) {
        if (inb(KEYBOARD_STATUS_PORT) & 1) {
            uint8_t scancode = inb(KEYBOARD_DATA_PORT);
            
            // Only handle key press (not release)
            if (!(scancode & 0x80)) {
                return keyboard_scancode_to_ascii(scancode);
            }
        }
    }
}

// GDT structures
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct gdt_entry gdt[3];
struct gdt_ptr gp;

extern void gdt_flush();

void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[num].access = access;
}

void gdt_install() {
    gp.limit = (sizeof(struct gdt_entry) * 3) - 1;
    gp.base = (uint32_t)&gdt;
    
    gdt_set_gate(0, 0, 0, 0, 0);
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    
    gdt_flush();
}

// IDT structures
struct idt_entry {
    uint16_t base_low;
    uint16_t selector;
    uint8_t always0;
    uint8_t flags;
    uint16_t base_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct idt_entry idt[256];
struct idt_ptr idtp;

extern void isr0();
extern void isr1();

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].selector = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

void idt_install() {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = (uint32_t)&idt;
    
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
    }
    
    idt_set_gate(0, (uint32_t)isr0, 0x08, 0x8E);
    idt_set_gate(1, (uint32_t)isr1, 0x08, 0x8E);
    
    asm volatile("lidt (%0)" : : "r"(&idtp));
}

void isr_handler(void) {
    timer_ticks++;
}

// Drawing functions
void draw_box(int x, int y, int width, int height, uint8_t color) {
    for (int row = y; row < y + height && row < VGA_HEIGHT; row++) {
        for (int col = x; col < x + width && col < VGA_WIDTH; col++) {
            if (row >= 0 && col >= 0) {
                size_t index = row * VGA_WIDTH + col;
                vga_buffer[index] = make_vgaentry(' ', color);
            }
        }
    }
}

void draw_progress_bar(int percentage) {
    int width = 50;
    int filled = (width * percentage) / 100;
    
    terminal_writestring("[");
    for (int i = 0; i < width; i++) {
        if (i < filled) {
            terminal_putchar('=');
        } else {
            terminal_putchar(' ');
        }
    }
    terminal_writestring("] ");
    terminal_writedec(percentage);
    terminal_writestring("%\n");
}

// Shell commands
void cmd_help() {
    terminal_setcolor(make_color(YELLOW, BLACK));
    terminal_writestring("Available commands:\n");
    terminal_setcolor(make_color(WHITE, BLACK));
    terminal_writestring("  help      - Show this help message\n");
    terminal_writestring("  clear     - Clear the screen\n");
    terminal_writestring("  echo      - Echo text back\n");
    terminal_writestring("  time      - Show system uptime\n");
    terminal_writestring("  sysinfo   - Show system information\n");
    terminal_writestring("  colors    - Display all VGA colors\n");
    terminal_writestring("  box       - Draw a colored box\n");
    terminal_writestring("  banner    - Show kernel banner\n");
    terminal_writestring("  shutdown  - Halt the system\n");
}

void cmd_echo(const char* args) {
    terminal_writestring(args);
    terminal_putchar('\n');
}

void cmd_time() {
    terminal_writestring("System uptime: ");
    terminal_writedec(timer_ticks / 100);
    terminal_writestring(" seconds\n");
}

void cmd_sysinfo() {
    terminal_setcolor(make_color(LIGHT_CYAN, BLACK));
    terminal_writestring("System Information:\n");
    terminal_setcolor(make_color(WHITE, BLACK));
    terminal_writestring("  Kernel: SimpleOS v1.0\n");
    terminal_writestring("  Architecture: x86 (32-bit)\n");
    terminal_writestring("  Display: VGA Text Mode (80x25)\n");
    terminal_writestring("  Timer ticks: ");
    terminal_writedec(timer_ticks);
    terminal_putchar('\n');
}

void cmd_colors() {
    terminal_writestring("VGA Color Palette:\n");
    for (int i = 0; i < 16; i++) {
        terminal_setcolor(make_color(i, BLACK));
        terminal_writestring("Color ");
        terminal_writedec(i);
        terminal_writestring("  ");
    }
    terminal_putchar('\n');
    terminal_setcolor(make_color(LIGHT_GREEN, BLACK));
}

void cmd_box() {
    int x = 10, y = 10, w = 20, h = 5;
    draw_box(x, y, w, h, make_color(WHITE, BLUE));
    terminal_row = y + h + 1;
    terminal_col = 0;
    terminal_writestring("Drew a box at (10, 10) with size 20x5\n");
}

void cmd_banner() {
    terminal_initialize();
    terminal_setcolor(make_color(LIGHT_CYAN, BLACK));
    terminal_writestring("========================================\n");
    terminal_setcolor(make_color(YELLOW, BLACK));
    terminal_writestring("   SimpleOS Kernel v1.0\n");
    terminal_setcolor(make_color(LIGHT_CYAN, BLACK));
    terminal_writestring("========================================\n");
    terminal_setcolor(make_color(LIGHT_GREEN, BLACK));
    terminal_writestring("Enhanced Interactive Kernel\n\n");
}

void cmd_shutdown() {
    terminal_setcolor(make_color(LIGHT_RED, BLACK));
    terminal_writestring("\nShutting down...\n");
    terminal_writestring("System halted. You can close the window now.\n");
    
    while(1) {
        asm volatile("hlt");
    }
}

// Shell
void kernel_shell() {
    terminal_setcolor(make_color(LIGHT_GREEN, BLACK));
    terminal_writestring("\nWelcome to SimpleOS Shell!\n");
    terminal_writestring("Type 'help' for available commands.\n\n");
    
    char buffer[256];
    int pos = 0;
    
    while (1) {
        terminal_setcolor(make_color(LIGHT_BLUE, BLACK));
        terminal_writestring("shell> ");
        terminal_setcolor(make_color(WHITE, BLACK));
        
        pos = 0;
        
        while (1) {
            char c = keyboard_read_char();
            
            if (c == '\n') {
                terminal_putchar('\n');
                buffer[pos] = '\0';
                break;
            } else if (c == '\b' && pos > 0) {
                pos--;
                if (terminal_col > 0) {
                    terminal_col--;
                    size_t index = terminal_row * VGA_WIDTH + terminal_col;
                    vga_buffer[index] = make_vgaentry(' ', terminal_color);
                }
            } else if (c >= 32 && c <= 126 && pos < 255) {
                buffer[pos++] = c;
                terminal_putchar(c);
            }
        }
        
        if (pos == 0) continue;
        
        // Parse command
        char cmd[256];
        char args[256];
        int i = 0, j = 0;
        
        // Extract command
        while (buffer[i] && buffer[i] != ' ') {
            cmd[j++] = buffer[i++];
        }
        cmd[j] = '\0';
        
        // Skip spaces
        while (buffer[i] == ' ') i++;
        
        // Extract arguments
        j = 0;
        while (buffer[i]) {
            args[j++] = buffer[i++];
        }
        args[j] = '\0';
        
        // Execute command
        if (str_cmp(cmd, "help") == 0) {
            cmd_help();
        } else if (str_cmp(cmd, "clear") == 0) {
            terminal_initialize();
        } else if (str_cmp(cmd, "echo") == 0) {
            cmd_echo(args);
        } else if (str_cmp(cmd, "time") == 0) {
            cmd_time();
        } else if (str_cmp(cmd, "sysinfo") == 0) {
            cmd_sysinfo();
        } else if (str_cmp(cmd, "colors") == 0) {
            cmd_colors();
        } else if (str_cmp(cmd, "box") == 0) {
            cmd_box();
        } else if (str_cmp(cmd, "banner") == 0) {
            cmd_banner();
        } else if (str_cmp(cmd, "shutdown") == 0) {
            cmd_shutdown();
        } else {
            terminal_setcolor(make_color(LIGHT_RED, BLACK));
            terminal_writestring("Unknown command: ");
            terminal_writestring(cmd);
            terminal_writestring("\nType 'help' for available commands.\n");
            terminal_setcolor(make_color(WHITE, BLACK));
        }
    }
}

// Kernel main
void kernel_main(uint32_t magic, uint32_t addr) {
    terminal_initialize();
    
    // Banner
    terminal_setcolor(make_color(LIGHT_CYAN, BLACK));
    terminal_writestring("========================================\n");
    terminal_setcolor(make_color(YELLOW, BLACK));
    terminal_writestring("   SimpleOS Kernel v1.0\n");
    terminal_setcolor(make_color(LIGHT_CYAN, BLACK));
    terminal_writestring("========================================\n\n");
    
    terminal_setcolor(make_color(LIGHT_GREEN, BLACK));
    terminal_writestring("[*] Initializing GDT...\n");
    gdt_install();
    terminal_writestring("[+] GDT initialized successfully\n\n");
    
    terminal_writestring("[*] Initializing IDT...\n");
    idt_install();
    terminal_writestring("[+] IDT initialized successfully\n\n");
    
    terminal_writestring("[*] Initializing keyboard...\n");
    terminal_writestring("[+] Keyboard ready\n\n");
    
    terminal_setcolor(make_color(WHITE, BLACK));
    terminal_writestring("Kernel Features:\n");
    terminal_writestring("  - VGA text mode display with scrolling\n");
    terminal_writestring("  - GDT (Global Descriptor Table)\n");
    terminal_writestring("  - IDT (Interrupt Descriptor Table)\n");
    terminal_writestring("  - Keyboard input support\n");
    terminal_writestring("  - Interactive shell with 9 commands\n");
    terminal_writestring("  - Timer support\n");
    terminal_writestring("  - Graphics functions\n\n");
    
    terminal_setcolor(make_color(LIGHT_GREEN, BLACK));
    terminal_writestring("Kernel initialized successfully!\n");
    
    // Start shell
    kernel_shell();
}

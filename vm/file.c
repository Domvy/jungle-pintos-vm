/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/process.h"

static bool file_backed_swap_in( struct page *page, void *kva );
static bool file_backed_swap_out( struct page *page );
static void file_backed_destroy( struct page *page );

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
    .swap_in = file_backed_swap_in,
    .swap_out = file_backed_swap_out,
    .destroy = file_backed_destroy,
    .type = VM_FILE,
};

/* The initializer of file vm */
void vm_file_init( void ) {}

/* Initialize the file backed page */
bool file_backed_initializer( struct page *page, enum vm_type type, void *kva ) {
    /* Set up the handler */
    page->operations = &file_ops;

    struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool file_backed_swap_in( struct page *page, void *kva ) { struct file_page *file_page UNUSED = &page->file; }

/* Swap out the page by writeback contents to the file. */
static bool file_backed_swap_out( struct page *page ) { struct file_page *file_page UNUSED = &page->file; }

/* Destory the file backed page. PAGE will be freed by the caller. */
static void file_backed_destroy( struct page *page ) { struct file_page *file_page UNUSED = &page->file; }

/* Do the mmap */
void *do_mmap( void *addr, size_t length, int writable, struct file *file, off_t offset ) {
    struct file *copy_file = file_reopen( file );
    void *start_addr = addr;

    struct page *head_page = NULL;
    struct page *prev_page = NULL;
    struct page *cur_page = NULL;

    while ( length > 0 ) {
        size_t page_read_bytes = length < PGSIZE ? length : PGSIZE;

        struct aux *aux = (struct aux *)malloc( sizeof( struct aux ) );
        aux->file = copy_file;
        aux->offset = offset;
        aux->page_read_bytes = page_read_bytes;

        // Create UNINIT page
        struct page *cur_page = vm_alloc_page_with_initializer( VM_FILE, addr, writable, lazy_load_segment, aux );
        if ( !cur_page ) return false;
        if ( head_page == NULL )
            head_page = cur_page;

        // Add file info into page
        cur_page->file.file = copy_file;
        cur_page->file.offset = offset;
        cur_page->file.page_read_bytes = page_read_bytes;
        cur_page->file.next_page = NULL;
        // Link same file group
        prev_page->file.next_page = cur_page;
        prev_page = cur_page;

        length -= page_read_bytes;
        addr += PGSIZE;
        offset += page_read_bytes;
    }

    cur_page->file.next_page = head_page;

    return start_addr;
}

#include "threads/mmu.h"
#include "kernel/list.h"

/* Do the munmap */
void do_munmap( void *addr ) {
    struct thread *t = thread_current();

    // find pte
    struct page *page = spt_find_page( &t->spt, addr );
    struct frame *frame = page->frame;

    // if dirty bit; file write and reset dirty bit
    void *upage = pg_round_down( addr );
    bool modified = pml4_is_dirty( t->pml4, upage );
    if ( modified ) {
        const struct file *file = page->file.file;
        const void *buffer = page->va;
        const off_t size = page->file.page_read_bytes;  // read_bytes 만큼이라, padding 제거는 자연스럽게
        const off_t file_ofs = page->file.offset;
        file_write_at( file, buffer, size, file_ofs );
        pml4_set_dirty( t->pml4, upage, true );
    }

    // delete page, frame
    spt_remove_page( &t->spt, page );
    list_remove( frame );

    // iter next page - same file backed
    struct page *next_page = page->file.next_page;
    if ( next_page ) {
        do_munmap( next_page->va );
    }
}

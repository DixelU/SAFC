#ifndef allocator_def
#define allocator_def

#include <memory>

namespace moya_alloc {
    template <class T, std::size_t grow_size = 1024>
    class mem_pool {
        struct node {
            node *next;
        };

        class buffer {
            static const std::size_t block_size = sizeof(T) > sizeof(node) ? sizeof(T) : sizeof(node);
            uint8_t data[block_size * grow_size];
        public:
            buffer *const next;
            buffer(buffer *next) :
                next(next) {

            }

            T *get_block(std::size_t index) {
                return reinterpret_cast<T *>(&data[block_size * index]);
            }
        };

        node *first_free_block = nullptr;
        buffer *first_buffer = nullptr;
        std::size_t buffered_blocks = grow_size;
    public:

        mem_pool() = default;
        mem_pool(mem_pool &&memory_pool) = delete;
        mem_pool(const mem_pool &memory_pool) = delete;
        mem_pool operator =(mem_pool &&memory_pool) = delete;
        mem_pool operator =(const mem_pool &memory_pool) = delete;

        ~mem_pool() {
            while (first_buffer) {
                buffer *buffer = first_buffer;
                first_buffer = buffer->next;
                delete buffer;
            }
        }

        T *allocate() {
            if (first_free_block) {
                node *block = first_free_block;
                first_free_block = block->next;
                return reinterpret_cast<T *>(block);
            }

            if (buffered_blocks >= grow_size) {
                first_buffer = new buffer(first_buffer);
                buffered_blocks = 0;
            }

            return first_buffer->get_block(buffered_blocks++);
        }

        void deallocate(T *pointer)
        {
            node *block = reinterpret_cast<node *>(pointer);
            block->next = first_free_block;
            first_free_block = block;
        }
    };

    template <class T, std::size_t grow_size = 1024>
    class allocator : private mem_pool<T, grow_size> {
    #ifdef _WIN32
        allocator *copy_allocator = nullptr;
        std::allocator<T> *rebind_allocator = nullptr;
    #endif

        public:

            typedef std::size_t size_type;
            typedef std::ptrdiff_t difference_type;
            typedef T *pointer;
            typedef const T *const_pointer;
            typedef T &reference;
            typedef const T &const_reference;
            typedef T value_type;

            template <class U>
            struct rebind {
                typedef allocator<U, grow_size> other;
            };

    #ifdef _WIN32
            allocator() = default;

            allocator(allocator &allocator) :
                copy_allocator(&allocator) {
            }

            template <class U>
            allocator(const allocator<U, grow_size> &other) {
                if (!std::is_same<T, U>::value)
                    rebind_allocator = new std::allocator<T>();
            }

            ~allocator() {
                delete rebind_allocator;
            }
    #endif

            pointer allocate(size_type n, const void *hint = 0) {
    #ifdef _WIN32
                if (copy_allocator)
                    return copy_allocator->allocate(n, hint);

                if (rebind_allocator)
                    return rebind_allocator->allocate(n, hint);
    #endif

                if (n != 1 || hint)
                    throw std::bad_alloc();

                return mem_pool<T, grow_size>::allocate();
            }

            void deallocate(pointer p, size_type n) {
    #ifdef _WIN32
                if (copy_allocator) {
                    copy_allocator->deallocate(p, n);
                    return;
                }

                if (rebind_allocator) {
                    rebind_allocator->deallocate(p, n);
                    return;
                }
    #endif

                mem_pool<T, grow_size>::deallocate(p);
            }

            void construct(pointer p, const_reference val) {
                new (p) T(val);
            }

            void destroy(pointer p) {
                p->~T();
            }
    };

}

#endif

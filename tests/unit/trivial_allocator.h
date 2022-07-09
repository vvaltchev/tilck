/* SPDX-License-Identifier: BSD-2-Clause */

#include <memory>
#include <cstdlib>

struct TrivialAllocatorData {

   char *buf;
   size_t used;
   size_t buf_size;

   ~TrivialAllocatorData()
   {
      free(buf);
   }
};

template <class T>
class MyTrivialAllocator {

public:

   using value_type = T;
   std::shared_ptr<TrivialAllocatorData> data;

   MyTrivialAllocator() noexcept { }
   MyTrivialAllocator(size_t sz) noexcept
   {
      if (sz == 0)
         return;

      data = std::make_shared<TrivialAllocatorData>();
      data->buf = reinterpret_cast<char *>(malloc(sz));

      if (!data->buf) {
         data.reset();
         return;
      }

      data->buf_size = sz;
      data->used = 0;
   }

   ~MyTrivialAllocator() noexcept = default;

   template<class U>
   MyTrivialAllocator(const MyTrivialAllocator<U>& rhs) noexcept
   {
      data = rhs.data;
   }

   T *allocate (std::size_t elems_count)
   {
      const size_t n = elems_count * sizeof(value_type);

      if (!data)
         return nullptr;

      if (data->used + n > data->buf_size) {
         return nullptr;
      }

      T *ret = reinterpret_cast<T *>(data->buf + data->used);
      data->used += n;
      return ret;
   }

   void deallocate (T* p, std::size_t n)
   {
      /* do nothing */
   }
};

template <class T, class U>
bool operator==(const MyTrivialAllocator<T>& a,
                const MyTrivialAllocator<U>& b) noexcept
{
   return true;
}

template <class T, class U>
bool operator!=(const MyTrivialAllocator<T>& a,
                const MyTrivialAllocator<U>& b) noexcept
{
   return false;
}


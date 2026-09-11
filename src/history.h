#pragma once

#include <cstddef>

// Fixed-size ring buffer feeding the on-screen graphs
template <size_t N>
class History {
 public:
  void push(float v) {
    data_[head_] = v;
    head_ = (head_ + 1) % N;
    if (count_ < N) count_++;
  }
  size_t size() const { return count_; }
  float at(size_t i) const { return data_[(head_ + N - count_ + i) % N]; }  // 0 = oldest

 private:
  float data_[N] = {};
  size_t head_ = 0;
  size_t count_ = 0;
};

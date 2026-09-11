#pragma once

#include <cfloat>
#include <cmath>

// Streaming min / max / mean / standard deviation (Welford's method)
class RunningStats {
 public:
  void add(double x) {
    n_++;
    double delta = x - mean_;
    mean_ += delta / n_;
    sumSq_ += delta * (x - mean_);
    if (x < min_) min_ = x;
    if (x > max_) max_ = x;
  }
  unsigned long count() const { return n_; }
  double mean() const { return mean_; }
  double stddev() const { return n_ > 1 ? std::sqrt(sumSq_ / (n_ - 1)) : 0; }
  double minVal() const { return n_ ? min_ : 0; }
  double maxVal() const { return n_ ? max_ : 0; }

 private:
  unsigned long n_ = 0;
  double mean_ = 0;
  double sumSq_ = 0;
  double min_ = DBL_MAX;
  double max_ = -DBL_MAX;
};

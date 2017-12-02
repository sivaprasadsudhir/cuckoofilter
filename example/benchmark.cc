// This benchmark reports on the bulk insert and bulk query rates. It is invoked as:
//
//     ./bulk-insert-and-query.exe 158000
//
// That invocation will test each probabilistic membership container type with 158000
// randomly generated items. It tests bulk Add() from empty to full and Contain() on
// filters with varying rates of expected success. For instance, at 75%, three out of
// every four values passed to Contain() were earlier Add()ed.
//
// Example output:
//
// $ for num in 55 75 85; do echo $num:; /usr/bin/time -f 'time: %e seconds' ./bulk-insert-and-query.exe ${num}00000; echo; done
// 55:
//                   Million    Find    Find    Find    Find    Find                       optimal  wasted
//                  adds/sec      0%     25%     50%     75%    100%       ε  bits/item  bits/item   space
//      Cuckoo12       23.78   37.24   35.04   37.17   37.35   36.35  0.131%      18.30       9.58   91.1%
//    SemiSort13       11.63   17.55   17.08   17.14   17.54   22.32  0.064%      18.30      10.62   72.4%
//       Cuckoo8       35.31   49.32   50.24   49.98   48.32   50.49  2.044%      12.20       5.61  117.4%
//     SemiSort9       13.99   22.23   22.78   22.13   23.16   24.06  1.207%      12.20       6.37   91.5%
//      Cuckoo16       27.06   36.94   37.12   35.31   36.81   35.10  0.009%      24.40      13.46   81.4%
//    SemiSort17       10.37   15.70   15.84   15.78   15.55   15.93  0.004%      24.40      14.72   65.8%
//    SimdBlock8       74.22   72.34   74.23   74.34   74.69   74.32  0.508%      12.20       7.62   60.1%
// time: 14.34 seconds
//
// 75:
//                   Million    Find    Find    Find    Find    Find                       optimal  wasted
//                  adds/sec      0%     25%     50%     75%    100%       ε  bits/item  bits/item   space
//      Cuckoo12       15.61   37.24   37.23   37.34   37.15   37.36  0.173%      13.42       9.18   46.2%
//    SemiSort13        8.77   17.11   15.70   17.34   17.73   18.86  0.087%      13.42      10.17   31.9%
//       Cuckoo8       23.46   48.81   48.14   39.48   49.28   49.65  2.806%       8.95       5.16   73.6%
//     SemiSort9       11.14   23.98   20.80   23.37   24.35   21.41  1.428%       8.95       6.13   46.0%
//      Cuckoo16       15.08   36.64   36.75   36.83   36.59   36.74  0.011%      17.90      13.11   36.5%
//    SemiSort17        8.02   15.63   15.66   15.87   15.67   15.88  0.006%      17.90      14.02   27.6%
//    SimdBlock8       73.26   74.41   74.28   70.86   72.02   70.69  2.071%       8.95       5.59   60.0%
// time: 18.06 seconds
//
// 85:
//                   Million    Find    Find    Find    Find    Find                       optimal  wasted
//                  adds/sec      0%     25%     50%     75%    100%       ε  bits/item  bits/item   space
//      Cuckoo12       22.74   32.49   32.69   32.58   32.85   32.71  0.102%      23.69       9.94  138.3%
//    SemiSort13        9.97   13.16   13.15   13.54   16.01   19.58  0.056%      23.69      10.80  119.4%
//       Cuckoo8       30.67   36.86   36.79   37.09   36.97   36.87  1.581%      15.79       5.98  163.9%
//     SemiSort9       10.96   15.49   15.37   15.40   15.18   15.63  1.047%      15.79       6.58  140.1%
//      Cuckoo16       27.84   33.74   33.72   33.69   33.75   33.62  0.007%      31.58      13.80  128.8%
//    SemiSort17        9.51   12.83   12.80   12.64   12.86   12.50  0.004%      31.58      14.65  115.6%
//    SimdBlock8       54.84   58.37   59.73   59.13   60.11   60.12  0.144%      15.79       9.44   67.3%
// time: 19.43 seconds
//

#include <climits>
#include <iomanip>
#include <map>
#include <stdexcept>
#include <vector>
#include <libcuckoo/cuckoohash_map.hh>

#include "cuckoofilter.h"
#include "random.h"
#include "timing.h"

using namespace std;

using namespace cuckoofilter;

// The number of items sampled when determining the lookup performance
const size_t SAMPLE_SIZE = 1000 * 1000;

// The statistics gathered for each table type:
struct Statistics {
  double adds_per_nano;
  map<int, double> finds_per_nano; // The key is the percent of queries that were expected
                                   // to be positive
  double false_positive_probabilty;
  double false_positive_rate;
  double bits_per_item;
};

// Output for the first row of the table of results. type_width is the maximum number of
// characters of the description of any table type, and find_percent_count is the number
// of different lookup statistics gathered for each table. This function assumes the
// lookup expected positive probabiilties are evenly distributed, with the first being 0%
// and the last 100%.
string StatisticsTableHeader(int type_width, int find_percent_count) {
  ostringstream os;

  os << string(type_width, ' ');
  os << setw(12) << right << "Million";
  for (int i = 0; i < find_percent_count; ++i) {
    os << setw(8) << "Find";
  }
  os << setw(8) << "" << setw(11) << "" << setw(12) << "" << setw(11)
     << "optimal" << setw(8) << "wasted" << endl;

  os << string(type_width, ' ');
  os << setw(12) << right << "adds/sec";
  for (int i = 0; i < find_percent_count; ++i) {
    os << setw(7)
       << static_cast<int>(100 * i / static_cast<double>(find_percent_count - 1)) << '%';
  }
  os << setw(9) << "ε" << setw(9) <<"εr" << setw(11) << "bits/item" << setw(11)
     << "bits/item" << setw(8) << "space";
  return os.str();
}

// Overloading the usual operator<< as used in "std::cout << foo", but for Statistics
template <class CharT, class Traits>
basic_ostream<CharT, Traits>& operator<<(
    basic_ostream<CharT, Traits>& os, const Statistics& stats) {
  constexpr double NANOS_PER_MILLION = 1000;
  os << fixed << setprecision(2) << setw(12) << right
     << stats.adds_per_nano * NANOS_PER_MILLION;
  for (const auto& fps : stats.finds_per_nano) {
    os << setw(8) << fps.second * NANOS_PER_MILLION;
  }
  const auto minbits = log2(1 / stats.false_positive_rate);
  os << setw(7) << setprecision(3) << stats.false_positive_probabilty * 100 << '%'
     << setprecision(3) << stats.false_positive_rate * 100 << '%'
     << setw(11) << setprecision(2) << stats.bits_per_item << setw(11) << minbits
     << setw(7) << setprecision(1) << 100 * (stats.bits_per_item / minbits - 1) << '%';

  return os;
}

template<typename Table>
struct FilterAPI {};

template <typename ItemType, size_t bits_per_item, template <size_t> class TableType>
struct FilterAPI<CuckooFilter<ItemType, bits_per_item, TableType>> {
  using Table = CuckooFilter<ItemType, bits_per_item, TableType>;
  static Table ConstructFromAddCount(size_t add_count) { return Table(add_count); }
  static void Add(uint64_t key, Table * table) {
    if (0 != table->Add(key)) {
      throw logic_error("The filter is too small to hold all of the elements");
    }
  }
  static bool Contain(uint64_t key, const Table * table) {
    return (0 == table->Contain(key));
  }
};


// template <typename ItemType, typename ValueType, size_t bits_per_item>
// class FilteredCuckooHash {
// public:
//   CuckooFilter<ItemType, bits_per_item> filter;
//   cuckoohash_map<ItemType, ValueType> hashmap;

//   FilteredCuckooHash(int total_items): filter(total_items) {
//     hashmap.reserve(total_items);
//   }

//   bool find(const ItemType &key) const {
//     return filter.Contain(key) == cuckoofilter::Ok;
//   }

//   bool contains(const ItemType &key) const {
//     if(filter.Contain(key) != cuckoofilter::Ok)
//       return false;
//     return hashmap.contains(key);
//   }

//   bool insert(const ItemType &key, const ValueType &val) {
//     if(filter.Add(key) != cuckoofilter::Ok)
//       return false;
//     if(hashmap.insert(key, val))
//       return true;
//     if(filter.Delete(key) == cuckoofilter::Ok)
//       return false;
//     // Should not reach here, throw an exception
//     return false;
//   }

//   bool erase(const ItemType &key) {
//     if(filter.Delete(key) != cuckoofilter::Ok)
//       return false;
//     if(hashmap.erase(key))
//       return true;
//     if(filter.Add(key) == cuckoofilter::Ok)
//       return false;
//     // Should not reach here, throw an exception
//     return false;
//   }

//   size_t SizeInBytes() const { return filter.SizeInBytes(); }
// };


template <typename ItemType, typename ValueType, size_t bits_per_item>
Statistics FilterBenchmark(
    size_t add_count, const vector<uint64_t>& to_add, const vector<uint64_t>& to_lookup) {
  if (add_count > to_add.size()) {
    throw out_of_range("to_add must contain at least add_count values");
  }

  if (SAMPLE_SIZE > to_lookup.size()) {
    throw out_of_range("to_lookup must contain at least SAMPLE_SIZE values");
  }

  //Table filter = FilterAPI<Table>::ConstructFromAddCount(add_count);
  CuckooFilter<ItemType, 12> filter(add_count);
  // FilteredCuckooHash <ItemType, ValueType, bits_per_item> filter(add_count);
  Statistics result;

  // Add values until failure or until we run out of values to add:
  auto start_time = NowNanos();
  for (size_t added = 0; added < add_count; ++added) {
    //FilterAPI<Table>::Add(to_add[added], &filter);
    filter.insert(to_add[added], to_add[added]);
  }
  result.adds_per_nano = add_count / static_cast<double>(NowNanos() - start_time);
  result.bits_per_item = static_cast<double>(CHAR_BIT * filter.SizeInBytes()) / add_count;

  size_t found_count = 0;
  size_t found_count_filter = 0;
  for (const double found_probability : {0.0, 0.25, 0.50, 0.75, 1.00}) {
    const auto to_lookup_mixed = MixIn(&to_lookup[0], &to_lookup[SAMPLE_SIZE], &to_add[0],
        &to_add[add_count], found_probability);
    for (const auto v : to_lookup_mixed) {
      //found_count += FilterAPI<Table>::Contain(v, &filter);
      ValueType val;
      int temp1, temp2, temp3;
      temp1 = filter.findinfilter(v);
      found_count += temp1;
      temp2 = filter.find(v, val);
      //found_count += filter.find(v, val);
      temp3 = filter.findinfilter(v);
      found_count_filter += temp3;
      if(temp1 == 1 && temp2 == 0 && temp3==1) {
        // std::cout << "ALERT" << std::endl;
      }
    }
    if (0.0 == found_probability) {
      result.false_positive_rate =
          found_count_filter / static_cast<double>(to_lookup_mixed.size());
    }
    if (0.0 == found_probability) {
      result.false_positive_probabilty =
          found_count / static_cast<double>(to_lookup_mixed.size());
    }
  }

  std::cout << "false_positive_rate = " << result.false_positive_rate << "\n";
  std::cout << "false_positive_probabilty = " << result.false_positive_probabilty << "\n";
  // found_count = 0;
  // for (const double found_probability : {0.0, 0.25, 0.50, 0.75, 1.00}) {
  //   const auto to_lookup_mixed = MixIn(&to_lookup[0], &to_lookup[SAMPLE_SIZE], &to_add[0],
  //       &to_add[add_count], found_probability);
  //   const auto start_time = NowNanos();
  //   for (const auto v : to_lookup_mixed) {
  //     //found_count += FilterAPI<Table>::Contain(v, &filter);
  //     found_count += filter.contains(v);
  //   }
  //   const auto lookup_time = NowNanos() - start_time;
  //   result.finds_per_nano[100 * found_probability] =
  //       SAMPLE_SIZE / static_cast<double>(lookup_time);
  //   if (0.0 == found_probability) {
  //     result.false_positive_probabilty =
  //         found_count / static_cast<double>(to_lookup_mixed.size());
  //   }
  // }

  found_count = 0;


  return result;
}

int main(int argc, char * argv[]) {
  if (argc != 2) {
    cerr << "Usage: " << argv[0] << " $NUMBER" << endl;
    return 1;
  }
  stringstream input_string(argv[1]);
  size_t add_count;
  input_string >> add_count;
  if (input_string.fail()) {
    cerr << "Invalid number: " << argv[1];
    return 2;
  }

  const vector<uint64_t> to_add = GenerateRandom64(add_count);
  const vector<uint64_t> to_lookup = GenerateRandom64(SAMPLE_SIZE);

  constexpr int NAME_WIDTH = 13;

  cout << StatisticsTableHeader(NAME_WIDTH, 5) << endl;

  auto cf = FilterBenchmark<uint64_t, uint64_t, 12>(
      add_count, to_add, to_lookup);

  cout << setw(NAME_WIDTH) << "Cuckoo12" << cf << endl;

  // cf = FilterBenchmark<uint64_t, uint64_t, 8>(
  //     add_count, to_add, to_lookup);

  // cout << setw(NAME_WIDTH) << "Cuckoo8" << cf << endl;

  // cf = FilterBenchmark<uint64_t, uint64_t, 16>(
  //     add_count, to_add, to_lookup);

  // cout << setw(NAME_WIDTH) << "Cuckoo16" << cf << endl;


}

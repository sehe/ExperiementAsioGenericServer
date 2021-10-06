#include "boost/accumulators/accumulators.hpp"
#include "boost/accumulators/statistics.hpp"
#include <thread>

namespace statistics {
    namespace ba  = boost::accumulators;
    namespace bat = boost::accumulators::tag;

    struct Accum {

        Accum(std::string name, size_t period)
            : _name(std::move(name))
            , _period(period)
        {
        }

        void sample(uint64_t nanos)
        {
            std::lock_guard lk(_mx);
            _accum(nanos);

            if (auto n = ba::count(_accum); n % _period == 0) {
                std::cout << _name << " n=" << n
                          << " median=" << (1ns * ba::median(_accum) / 1.0ms)
                          << " avg=" << (1ns * ba::mean(_accum) / 1.0ms)
                          << "ms stddev="
                          << (sqrt(ba::variance(_accum)) / 1'000'000) << "ms"
                          << std::endl;
            }
        }

      private:
        std::string _name;
        size_t      _period;
        std::mutex  _mx;
        ba::accumulator_set<double, ba::stats<bat::median, bat::variance>>
            _accum;
    };

    Accum g_latencies{"Latency", 4'000};
    Accum g_roundtrips{"Roundtrip", 100};
} // namespace statistics


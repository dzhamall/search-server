#pragma once

#include <vector>
#include <iostream>

template <typename Iterator>
class IteratorRange {
public:
    explicit IteratorRange(Iterator it_begin, Iterator it_end)
            : it_begin_(it_begin), it_end_(it_end)
    {}

    auto begin() const {
        return it_begin_;
    }

    auto end() const {
        return it_end_;
    }

    auto size() const {
        return distance(it_begin_, it_end_);
    }

private:
    Iterator it_begin_;
    Iterator it_end_;
};

template <typename Iterator>
class Paginator {
public:
    explicit Paginator(const size_t page_size, Iterator begin, Iterator end) {
        pages = distance(begin, end) / page_size;
        for (auto it = begin; it < end; it++) {
            if (distance(it, end) < page_size) {
                iterators.push_back(IteratorRange(it, end));
                break;
            } else {
                IteratorRange<Iterator> page(it, next(it, page_size));
                iterators.push_back(page);
                advance(it, page_size - 1);
            }
        }
    }

    auto begin() const {
        return iterators.begin();
    }

    auto end() const {
        return iterators.end();
    }

    auto size() const {
        return pages;
    }

private:
    std::vector<IteratorRange<Iterator>> iterators;
    size_t pages;
};

template <typename Iterator>
IteratorRange<Iterator>& operator++(IteratorRange<Iterator>& it) {
    return { it.begin() + it.size(), it.end() + it.size() };
}

template <typename Iterator>
std::ostream& operator<<(std::ostream& output, const IteratorRange<Iterator>& iterator_range) {
    for (auto it = iterator_range.begin(); it != iterator_range.end(); it++) {
        output << *it;
    }
    return output;
}

template <typename Container>
auto Paginate(const Container& c, size_t page_size) {
    return Paginator(page_size, c.begin(), c.end());
}
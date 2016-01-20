//
//  read_manager.hpp
//  Octopus
//
//  Created by Daniel Cooke on 14/02/2015.
//  Copyright (c) 2015 Oxford University. All rights reserved.
//

#ifndef __Octopus__read_manager__
#define __Octopus__read_manager__

#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <initializer_list>
#include <cstddef>
#include <mutex>

#include <boost/filesystem.hpp>

#include "contig_region.hpp"
#include "genomic_region.hpp"
#include "mappable_map.hpp"
#include "read_reader.hpp"
#include "read_reader_impl.hpp"
#include "hash_functions.hpp"

class AlignedRead;

// TODO: make this thread-safe

class ReadManager
{
public:
    using Path = boost::filesystem::path;
    
    using SampleIdType  = IReadReaderImpl::SampleIdType;
    using Reads         = IReadReaderImpl::Reads;
    using SampleReadMap = IReadReaderImpl::SampleReadMap;
    
    ReadManager() = default;
    explicit ReadManager(std::vector<Path> read_file_paths, unsigned max_open_files);
    explicit ReadManager(std::initializer_list<Path> read_file_paths);
    ~ReadManager() = default;
    
    ReadManager(const ReadManager&)            = delete;
    ReadManager& operator=(const ReadManager&) = delete;
    ReadManager(ReadManager&&);
    ReadManager& operator=(ReadManager&&)      = default;
    
    friend void swap(ReadManager& lhs, ReadManager& rhs) noexcept;
    
    unsigned num_samples() const noexcept;
    std::vector<SampleIdType> get_samples() const;
    
    size_t count_reads(const SampleIdType& sample, const GenomicRegion& region);
    size_t count_reads(const std::vector<SampleIdType>& samples, const GenomicRegion& region);
    size_t count_reads(const GenomicRegion& region); // all samples
    
    GenomicRegion find_covered_subregion(const SampleIdType& sample, const GenomicRegion& region,
                                         size_t max_sample_coverage);
    GenomicRegion find_covered_subregion(const std::vector<SampleIdType>& samples,
                                         const GenomicRegion& region, size_t max_sample_coverage);
    GenomicRegion find_covered_subregion(const GenomicRegion& region, size_t max_sample_coverage);
    
    Reads fetch_reads(const SampleIdType& sample, const GenomicRegion& region);
    SampleReadMap fetch_reads(const std::vector<SampleIdType>& samples, const GenomicRegion& region);
    SampleReadMap fetch_reads(const GenomicRegion& region); // all samples
    
private:
    struct FileSizeCompare
    {
        bool operator()(const Path& lhs, const Path& rhs) const;
    };
    
    using OpenReaderMap           = std::map<Path, ReadReader, FileSizeCompare>;
    using ClosedReaders           = std::unordered_set<Path>;
    using SampleIdToReaderPathMap = std::unordered_map<SampleIdType, std::vector<Path>>;
    using ContigMap               = MappableMap<GenomicRegion::ContigNameType, ContigRegion>;
    using ReaderRegionsMap        = std::unordered_map<Path, ContigMap>;
    
    const unsigned max_open_files_ = 200;
    const unsigned num_files_;
    OpenReaderMap open_readers_;
    ClosedReaders closed_readers_;
    SampleIdToReaderPathMap reader_paths_containing_sample_;
    ReaderRegionsMap possible_regions_in_readers_;
    
    mutable std::mutex mutex_;
    
    void setup();
    std::vector<Path> get_bad_paths() const;
    void setup_reader_samples_and_regions();
    void open_initial_files();
    
    ReadReader make_reader(const Path& reader_path);
    bool is_open(const Path& reader_path) const noexcept;
    std::vector<Path>::iterator partition_open(std::vector<Path>& reader_paths) const;
    unsigned num_open_readers() const noexcept;
    unsigned num_reader_spaces() const noexcept;
    void open_reader(const Path& reader_path);
    std::vector<Path>::iterator open_readers(std::vector<Path>::iterator first,
                                             std::vector<Path>::iterator last);
    void close_reader(const Path& reader_path);
    Path choose_reader_to_close() const;
    void close_readers(unsigned n);
    
    void add_possible_regions_to_reader_map(const Path& reader_path,
                                            const std::vector<GenomicRegion>& regions);
    void add_reader_to_sample_map(const Path& reader_path,
                                  const std::vector<SampleIdType>& samples_in_reader);
    bool could_reader_contain_region(const Path& reader_path,
                                     const GenomicRegion& region) const;
    
    std::vector<Path> get_reader_paths_containing_samples(const std::vector<SampleIdType>& sample) const;
    std::vector<Path> get_reader_paths_possibly_containing_region(const GenomicRegion& region) const;
    std::vector<Path> get_possible_reader_paths(const std::vector<SampleIdType>& samples,
                                                const GenomicRegion& region) const;
};

#endif /* defined(__Octopus__read_manager__) */

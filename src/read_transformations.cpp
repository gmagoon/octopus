//
//  read_transformations.cpp
//  Octopus
//
//  Created by Daniel Cooke on 01/04/2016.
//  Copyright © 2016 Oxford University. All rights reserved.
//

#include "read_transformations.hpp"

namespace Octopus
{
namespace ReadTransforms
{
    void MaskOverlappedSegment::operator()(AlignedRead& read) const noexcept
    {
        // Only reads in the forward direction are masked to prevent double masking
        if (read.is_chimeric() && !read.is_marked_reverse_mapped()) {
            const auto next_segment_begin = read.next_segment().begin();
            
            if (next_segment_begin < mapped_end(read)) {
                const auto overlapped_size = mapped_end(read) - next_segment_begin;
                read.zero_back_qualities(overlapped_size);
            }
        }
    }
    
    void MaskAdapters::operator()(AlignedRead& read) const noexcept
    {
        if (read.is_chimeric()) {
            const auto insert_size = read.next_segment().inferred_template_length();
            const auto read_size   = sequence_size(read);
            
            if (insert_size <= read_size) {
                const auto num_adapter_bases = read_size - insert_size;
                
                if (read.is_marked_reverse_mapped()) {
                    read.zero_back_qualities(num_adapter_bases);
                } else {
                    read.zero_front_qualities(num_adapter_bases);
                }
            }
        }
    }
    
    MaskTail::MaskTail(SizeType num_bases) : num_bases_ {num_bases} {};
    
    void MaskTail::operator()(AlignedRead& read) const noexcept
    {
        if (read.is_marked_reverse_mapped()) {
            read.zero_front_qualities(num_bases_);
        } else {
            read.zero_back_qualities(num_bases_);
        }
    }
    
    void MaskSoftClipped::operator()(AlignedRead& read) const noexcept
    {
        if (is_soft_clipped(read.cigar_string())) {
            const auto soft_clipped_sizes = get_soft_clipped_sizes(read.cigar_string());
            read.zero_front_qualities(soft_clipped_sizes.first);
            read.zero_back_qualities(soft_clipped_sizes.second);
        }
    }
    
    MaskSoftClippedBoundries::MaskSoftClippedBoundries(SizeType num_bases) : num_bases_ {num_bases} {};
    
    void MaskSoftClippedBoundries::operator()(AlignedRead& read) const noexcept
    {
        if (is_soft_clipped(read)) {
            const auto soft_clipped_sizes = get_soft_clipped_sizes(read);
            
            if (soft_clipped_sizes.first > 0) {
                read.zero_front_qualities(soft_clipped_sizes.first + num_bases_);
            }
            
            if (soft_clipped_sizes.second > 0) {
                read.zero_back_qualities(soft_clipped_sizes.second + num_bases_);
            }
        }
    }
} // namespace ReadTransforms
} // namespace Octopus

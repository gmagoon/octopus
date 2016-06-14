//
//  somatic_call.cpp
//  Octopus
//
//  Created by Daniel Cooke on 21/04/2016.
//  Copyright © 2016 Oxford University. All rights reserved.
//

#include "somatic_call.hpp"

#include "string_utils.hpp"

namespace Octopus
{
    void SomaticCall::decorate(VcfRecord::Builder& record) const
    {
        record.set_somatic();
        
        record.set_alt(variant_.alt_allele().sequence());
        
        record.add_format("SCR");
        
        for (const auto& p : credible_regions_) {
            if (p.second.somatic) {
                record.set_format(p.first, "SCR", {
                    Octopus::to_string(p.second.somatic->first, 2),
                    Octopus::to_string(p.second.somatic->second, 2)
                });
            } else {
                record.set_format(p.first, "SCR", {"0", "0"});
            }
        }
    }
} // namespace Octopus

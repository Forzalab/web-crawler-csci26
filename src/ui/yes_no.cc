// yes_no.cc - see yes_no.h
#include "ui/yes_no.h"

namespace wc {

std::optional<bool> yes_no_answer(char key, bool default_yes) {
    switch (key) {
        case 'y': case 'Y': return true;
        case 'n': case 'N': return false;
        case '\n': case '\r': return default_yes;
        default: return std::nullopt;
    }
}

}  // namespace wc

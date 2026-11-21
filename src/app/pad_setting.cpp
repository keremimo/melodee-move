// SPDX-License-Identifier: AGPL-3.0-only
#include "app/pad_setting.h"
#include "app/studio.h"

namespace app::pad_setting {

void apply(Studio& studio, PadLayout layout) {
    studio.say(said(layout));
    if (layout == pad_layout()) return;
    set_pad_layout(layout);
    studio.publish_pads(); // a note track's pad map (app/keys.h) in the new layout from the next pad, not the next lights pass
    studio.changed(); // Move.settings is written with the project, three seconds on
}

} // namespace app::pad_setting

#include "core/duel_weight_profiles_generated.h"

#include <assert.h>
#include <string.h>

int main(void) {
    assert(CoreDuelWeightProfileCount() == 2);
    const CoreDuelWeightProfile* default_profile = CoreDuelWeightProfileDefault();
    assert(default_profile != NULL);
    assert(strcmp(default_profile->name, "duel-default") == 0);
    assert(strcmp(default_profile->version, "1") == 0);
    assert(strcmp(default_profile->status, "production-default") == 0);
    assert(strlen(default_profile->sha256) == 64);
    assert(default_profile->weights.opponent_reachable_space == 0.0);
    assert(default_profile->weights.territory_delta == 0.0);
    assert(default_profile->weights.opponent_safe_moves == 0.0);
    assert(default_profile->weights.opponent_low_health_food_denial == 0.0);

    const CoreDuelWeightProfile* candidate = CoreDuelWeightProfileFind(
        "tuned-opponent-pressure", "1"
    );
    assert(candidate != NULL);
    assert(strcmp(candidate->status, "candidate") == 0);
    assert(CoreDuelWeightProfileAt(0) != NULL);
    assert(CoreDuelWeightProfileAt(CoreDuelWeightProfileCount()) == NULL);
    assert(CoreDuelWeightProfileFind("missing", "1") == NULL);
    assert(CoreDuelWeightProfileFind(NULL, "1") == NULL);
    return 0;
}

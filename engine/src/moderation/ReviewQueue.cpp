#include "moderation/ReviewQueue.hpp"

namespace engine::moderation {

void ReviewQueue::add(ReviewCase reviewCase) { cases_.push_back(std::move(reviewCase)); }

} // namespace engine::moderation

// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include <stdexcept>
#include <string>
#include <sampler.h>

namespace edge {

namespace {

rove::matrix<4,4> compose(rove::vec<3> const &translation, rove::quaternion<> const &rotation, rove::vec<3> const &scale)
{
	rove::matrix<4,4> m;
	m.identity();
	m.rotation(rotation);
	for (int column = 0; column < 3; ++column)
		for (int row = 0; row < 3; ++row)
			m.ij[column][row] *= scale.i[column];
	m.ij[3][0] = translation.x;
	m.ij[3][1] = translation.y;
	m.ij[3][2] = translation.z;
	return m;
}

}

void Sampler::add_sample(Key const &key)
{
	if (!samples_->empty() && !(key.time > samples_->back().key.time))
	{
		throw std::invalid_argument("a keyframe at " + std::to_string(key.time) + " after one at " + std::to_string(samples_->back().key.time));
	}

	if (samples_.use_count() > 1)      // shared with another copy: this one's keys become its own
		samples_ = std::make_shared<samples_type>(*samples_);

	Sample s;
	s.key = key;
	s.transform = compose(key.translation, key.rotation, key.scale);
	if (!samples_->empty())
		samples_->back().rotation_interpolator.setup(samples_->back().key.rotation, key.rotation);
	samples_->push_back(s);
}

rove::matrix<4,4> Sampler::get(rove::scalar time) const {
	if (samples_->empty())
		throw std::logic_error("a sampler of no keyframes (sid '" + sid_ + "')");
	if (time <= samples_->front().key.time) return samples_->front().transform;
	if (time >= samples_->back().key.time) return samples_->back().transform;

	samples_type::const_iterator const it = std::upper_bound(samples_->begin(), samples_->end(), time,
		[](rove::scalar t, Sample const &s) { return t < s.key.time; });
	Key const &a = (it - 1)->key, &b = it->key;

	rove::scalar const k = (time - a.time) / (b.time - a.time);
	rove::scalar const inv_k = 1.0f - k;
	return compose(a.translation * inv_k + b.translation * k, (it - 1)->rotation_interpolator.interpolate(k), a.scale * inv_k + b.scale * k);
}

}

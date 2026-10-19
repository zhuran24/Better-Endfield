"""Regression tests for near-tied search scores and deterministic tie breaking."""
import itertools
import unittest
import build_jsspsi_accessory as build


def metrics(speed, dv):
    return {60: {'speed': speed, 'dv': dv},
            120: {'speed': speed, 'dv': dv}}


class SelectionTests(unittest.TestCase):
    def test_roundoff_does_not_override_lower_speed(self):
        # Measured arm scores: primary differs only in floating-point roundoff.
        a = metrics(0.7765999135842843, 1.1422065675117115)
        b = metrics(0.8200078582639188, 1.1422065675115)
        self.assertLess(build.key_score(a), build.key_score(b))

    def test_canonical_parameters_break_an_exact_tie(self):
        m = metrics(0.8, 1.2)
        a = dict(method='hybrid', u3=.365, uk=.255, K=3, ridge=.003)
        b = dict(method='hybrid', u3=.365, uk=.27, K=3, ridge=.0001)
        expected = min([a, b], key=lambda c: build.candidate_key(m, c))
        for order in itertools.permutations([a, b]):
            self.assertEqual(min(order, key=lambda c: build.candidate_key(m, c)), expected)
        self.assertEqual(build.candidate_key(m, a),
                         build.candidate_key(m, dict(reversed(list(a.items())))))

    def test_refinement_seeds_use_secondary_scores(self):
        a = dict(method='hybrid', u3=.365, uk=.255, K=3, ridge=.003,
                 score=.7614710450078076, speed_60=.7765999135842843,
                 speed_120=.7722834049193158, dv_60=1.1240720887551041,
                 dv_120=1.1422065675117115)
        b = dict(method='hybrid', u3=.365, uk=.27, K=3, ridge=.0001,
                 score=.7614710450076667, speed_60=.8200078582639188,
                 speed_120=.7966821108326877, dv_60=1.0127924894075095,
                 dv_120=1.1422065675115)
        for order in itertools.permutations([a, b]):
            self.assertEqual(build.refinement_seeds(order), [a])

    def test_capture_hash_breaks_a_near_tie(self):
        a = dict(score=[1.5, .9], capture_sha256='a'*64)
        b = dict(score=[1.5-1e-13, .9], capture_sha256='b'*64)
        self.assertLess(build.capture_key(a), build.capture_key(b))

    def test_meaningful_score_gap_still_wins(self):
        self.assertLess(build.key_score(metrics(1.0, 1.0)),
                        build.key_score(metrics(.8, 1.8)))


if __name__ == '__main__':
    unittest.main()

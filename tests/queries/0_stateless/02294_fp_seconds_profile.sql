-- Tags: no-parallel
-- Bug: https://github.com/ClickHouse/ClickHouse/issues/38863

DROP SETTINGS PROFILE IF EXISTS _02294_profile1, _02294_profile2;

CREATE SETTINGS PROFILE _02294_profile1 SETTINGS timeout_before_checking_execution_speed = 3 TO default;
SHOW CREATE SETTINGS PROFILE _02294_profile1;

CREATE SETTINGS PROFILE _02294_profile2 SETTINGS max_execution_time = 0.5 TO default;
SHOW CREATE SETTINGS PROFILE _02294_profile2;

DROP SETTINGS PROFILE IF EXISTS _02294_profile1, _02294_profile2;

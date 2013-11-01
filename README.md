shard_id_udf
============

MySQL shard id generator udf

Adds the following functions:
-----------------------------
`next_shard_id(<shard>)` -- generates an id for a given shard (MAX of 1024 shards)

`shard_id_to_ms(<id>)` -- converts a generated id to ms since epoch 1970-01-01

Installing/Building:
====================
From shell (assuming in directory with Makefile and source):
```
% make
% sudo cp shard_id.so /usr/lib/mysql/plugin/
% mysql
mysql> create function next_shard_id returns integer soname 'shard_id.so';
mysql> create function shard_id_to_ms returns integer soname 'shard_id.so';
```

Notes:
======
Implementation based on Instagram sharding ids described at:

http://instagram-engineering.tumblr.com/post/10853187575

logical shard id and auto-increment bit sizes were flipped in this
implementation. Also, the timestamp portion is different with a 31-bit second
based timestamp and a 10-bit based millisecond-like number

This id generation scheme will provide up to:
8192 unique ids per shard per millisecond-like time interval until the morning
of June 19, 2081.

Examples:
=========
Assume the following table exists on two shards (shard 0 and shard 1):
```sql
CREATE TABLE `tags` (
    `id`  BIGINT UNSIGNED NOT NULL,
    `tag` VARCHAR(255)    NOT NULL,
    PRIMARY KEY (`id`),
    UNIQUE (`tag`)
) ENGINE=InnoDB;
```

Insert the tag 'a' into shard 0, and the tag 'b' into shard 1:
```sql
INSERT INTO tags (id, tag) VALUES (next_shard_id(0), 'a');
INSERT INTO tags (id, tag) VALUES (next_shard_id(1), 'b');
```
Will create a rows similar to:

`{"id": "113359367939555328", "tag": "a"}` on shard 0

`{"id": "113359694080245761", "tag": "b"}` on shard 1

To get `LAST_INSERT_ID` to work correctly, you can update the above statements to:
```sql
INSERT INTO tags (id, tag) VALUES (LAST_INSERT_ID(next_shard_id(0)), 'a');
INSERT INTO tags (id, tag) VALUES (LAST_INSERT_ID(next_shard_id(1)), 'b');
```

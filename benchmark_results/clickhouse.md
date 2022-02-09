# ClickHouse

## Build ClickHouse

```sh
git clone https://github.com/ClickHouse/ClickHouse.git
cd ClickHouse/
git submodule update --init --recursive
mkdir build
cd build
export CC=clang CXX=clang++
cmake -D CMAKE_BUILD_TYPE=Release ..
ninja clickhouse-server clickhouse-client
```

## Run ClickHouse Server

```sh
cd programs/server/
numactl --membind=0 --cpubind=0  ../../build/programs/clickhouse server
```

## Run ClickHouse Client

```sh
./build/programs/clickhouse client --multiline
```

## Load TPC-H Data

```sh
./build/programs/clickhouse client --multiline --multiquery
CREATE TABLE lineitem
(
    l_orderkey Int32 NOT NULL,
    l_partkey Int32 NOT NULL,
    l_suppkey Int32 NOT NULL,
    l_linenumber Int32 NOT NULL,
    l_quantity Decimal(12,2) NOT NULL,
    l_extendedprice Decimal(12,2) NOT NULL,
    l_discount Decimal(12,2) NOT NULL,
    l_tax Decimal(12,2) NOT NULL,
    l_returnflag FixedString(1) NOT NULL,
    l_linestatus FixedString(1) NOT NULL,
    l_shipdate Date NOT NULL,
    l_commitdate Date NOT NULL,
    l_receiptdate Date NOT NULL,
    l_shipinstruct FixedString(25) NOT NULL,
    l_shipmode FixedString(10) NOT NULL,
    l_comment String NOT NULL
) ENGINE = Memory;

CREATE TABLE part
(
   p_partkey Int32 NOT NULL,
   p_name String NOT NULL,
   p_mfgr FixedString(25) NOT NULL,
   p_brand FixedString(10) NOT NULL,
   p_type String NOT NULL,
   p_size Int32 NOT NULL,
   p_container FixedString(10) NOT NULL,
   p_retailprice Decimal(12,2) NOT NULL,
   p_comment String NOT NULL
) ENGINE = Memory;


SET max_memory_usage = 0;
SET format_csv_delimiter = '|';
INSERT INTO lineitem FROM INFILE '/raid0/data/tpch/sf10/lineitem.tbl' FORMAT CSV;
INSERT INTO part FROM INFILE '/raid0/data/tpch/sf10/part.tbl' FORMAT CSV;
```

## Queries

### Query 1

```
EXPLAIN SELECT
        l_returnflag,
        l_linestatus,
        sum(l_quantity) AS sum_qty,
        sum(l_extendedprice) AS sum_base_price,
        sum(l_extendedprice * (1 - l_discount)) AS sum_disc_price,
        sum(l_extendedprice * (1 - l_discount) * (1 + l_tax)) AS sum_charge,
        avg(l_quantity) AS avg_qty,
        avg(l_extendedprice) AS avg_price,
        avg(l_discount) AS avg_disc,
        count(*) AS count_order
FROM
        lineitem
WHERE
        l_shipdate <= date '1998-12-01' - interval '90' day
GROUP BY
        l_returnflag,
        l_linestatus
ORDER BY
        l_returnflag,
        l_linestatus;
```

### Query 14

```
EXPLAIN SYNTAX SELECT
        100.00 * sum(case
                when p_type like 'PROMO%'
                        then l_extendedprice * (1 - l_discount)
                else 0
        end) / sum(l_extendedprice * (1 - l_discount)) as promo_revenue
FROM part INNER ALL JOIN lineitem ON p_partkey = l_partkey
WHERE
    l_shipdate >= date '1995-09-01'
    and l_shipdate < date '1995-09-01' + interval '1' month;
```
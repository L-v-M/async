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
./build/programs/clickhouse client --multiline --multiquery
```

## Create schema

```
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

CREATE TABLE supplier
(
   s_suppkey Int32 NOT NULL,
   s_name FixedString(25) NOT NULL,
   s_address String NOT NULL,
   s_nationkey Int32 NOT NULL,
   s_phone FixedString(15) NOT NULL,
   s_acctbal Decimal(12,2) NOT NULL,
   s_comment String NOT NULL
) ENGINE = Memory;

CREATE TABLE partsupp
(
   ps_partkey Int32 NOT NULL,
   ps_suppkey Int32 NOT NULL,
   ps_availqty Int32 NOT NULL,
   ps_supplycost Decimal(12,2) NOT NULL,
   ps_comment String NOT NULL
) ENGINE = Memory;

CREATE TABLE customer
(
   c_custkey Int32 NOT NULL,
   c_name String NOT NULL,
   c_address String NOT NULL,
   c_nationkey Int32 NOT NULL,
   c_phone FixedString(15) NOT NULL,
   c_acctbal Decimal(12,2) NOT NULL,
   c_mktsegment FixedString(10) NOT NULL,
   c_comment String NOT NULL
) ENGINE = Memory;

CREATE TABLE orders
(
   o_orderkey Int32 NOT NULL,
   o_custkey Int32 NOT NULL,
   o_orderstatus FixedString(1) NOT NULL,
   o_totalprice Decimal(12,2) NOT NULL,
   o_orderdate Date NOT NULL,
   o_orderpriority FixedString(15) NOT NULL,
   o_clerk FixedString(15) NOT NULL,
   o_shippriority Int32 NOT NULL,
   o_comment String NOT NULL
) ENGINE = Memory;

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

CREATE TABLE nation
(
   n_nationkey Int32 NOT NULL,
   n_name FixedString(25) NOT NULL,
   n_regionkey Int32 NOT NULL,
   n_comment String NOT NULL
) ENGINE = Memory;

CREATE TABLE region
(
   r_regionkey Int32 NOT NULL,
   r_name FixedString(25) NOT NULL,
   r_comment String NOT NULL
) ENGINE = Memory;

```

## Load data

```
SET max_memory_usage = 0;
SET max_threads = 64;
SET format_csv_delimiter = '|';

INSERT INTO part FROM INFILE '/raid0/data/tpch/sf30/part.tbl' FORMAT CSV;
INSERT INTO supplier FROM INFILE '/raid0/data/tpch/sf30/supplier.tbl' FORMAT CSV;
INSERT INTO partsupp FROM INFILE '/raid0/data/tpch/sf30/partsupp.tbl' FORMAT CSV;
INSERT INTO customer FROM INFILE '/raid0/data/tpch/sf30/customer.tbl' FORMAT CSV;
INSERT INTO orders FROM INFILE '/raid0/data/tpch/sf30/orders.tbl' FORMAT CSV;
INSERT INTO lineitem FROM INFILE '/raid0/data/tpch/sf30/lineitem.tbl' FORMAT CSV;
INSERT INTO nation FROM INFILE '/raid0/data/tpch/sf30/nation.tbl' FORMAT CSV;
INSERT INTO region FROM INFILE '/raid0/data/tpch/sf30/region.tbl' FORMAT CSV;
```

## Queries

### Query 1

```
select
        l_returnflag,
        l_linestatus,
        sum(l_quantity) as sum_qty,
        sum(l_extendedprice) as sum_base_price,
        sum(l_extendedprice * (1 - l_discount)) as sum_disc_price,
        sum(l_extendedprice * (1 - l_discount) * (1 + l_tax)) as sum_charge,
        avg(l_quantity) as avg_qty,
        avg(l_extendedprice) as avg_price,
        avg(l_discount) as avg_disc,
        count(*) as count_order
from
        lineitem
where
        l_shipdate <= date '1998-12-01' - interval '90' day
group by
        l_returnflag,
        l_linestatus
order by
        l_returnflag,
        l_linestatus;
```

### Query 2

```
select
        s_acctbal,
        s_name,
        n_name,
        p_partkey,
        p_mfgr,
        s_address,
        s_phone,
        s_comment
from
        part,
        supplier,
        partsupp,
        nation,
        region,
      (
                select
                        min(ps_supplycost) min_ps, ps_partkey pr_key
                from
                        partsupp,
                        supplier,
                        nation,
                        region
                where
                        s_suppkey = ps_suppkey
                        and s_nationkey = n_nationkey
                        and n_regionkey = r_regionkey
                        and r_name = 'EUROPE'
                group by ps_partkey
        ) precomp
where
        p_partkey = ps_partkey
        and s_suppkey = ps_suppkey
        and p_size = 15
        and p_type like '%BRASS'
        and s_nationkey = n_nationkey
        and n_regionkey = r_regionkey
        and r_name = 'EUROPE'
        and ps_supplycost = min_ps
        and p_partkey = pr_key
order by
        s_acctbal desc,
        n_name,
        s_name,
        p_partkey
limit
        100;
```

```

SELECT min(ps_supplycost) AS min_ps, ps_partkey AS pr_key
FROM partsupp INNER ALL JOIN supplier ON ps_suppkey = s_suppkey INNER ALL JOIN nation ON s_nationkey = n_nationkey INNER ALL JOIN region ON n_regionkey = r_regionkey
WHERE r_name = 'EUROPE'
GROUP BY ps_partkey
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
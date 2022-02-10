library(tidyverse)

results <- read_csv("/Users/leonard/Desktop/tpch_q14.csv")

results %>%
  filter(num_threads %in% c(1, 2, 4, 8, 16, 32, 64) &
           num_tuples_per_coroutine %in% c(0, 1000)) %>%
  mutate(percent_cached = factor((num_cached_references * 100) %/% num_total_references)) %>%
  mutate(num_threads = factor(num_threads)) %>%
  mutate(page_size_power = recode(page_size_power,
                                  "12" = "Page size: 4KiB")) %>%
  mutate(
    num_threads = recode(
      num_threads,
      "1" = "1 Thread",
      "2" = "2 Threads",
      "4" = "4 Threads",
      "8" = "8 Threads",
      "16" = "16 Threads",
      "32" = "32 Threads",
      "64" = "64 Threads"
    )
  ) %>%
  ggplot() +
  geom_col(mapping = aes(
    x = percent_cached,
    y = time,
    fill = factor(num_entries_per_ring)
  ),
  position = "dodge") +
  facet_grid(cols = vars(num_threads)) +
  labs(x = "Fraction of index accesses that are a cache hit (in percent)",
       y = "Execution time (in ms)",
       fill = "Kind of I/O and I/O depth per thread") +
  scale_fill_discrete(
    limits = c("0", "8", "16", "32", "64", "128", "256", "512"),
    labels = c(
      "Sync.",
      "Async. (8)",
      "Async. (16)",
      "Async. (32)",
      "Async. (64)",
      "Async. (128)",
      "Async. (256)",
      "Async. (512)"
    )
  ) +
  scale_y_continuous(breaks = scales::pretty_breaks(n = 40)) +
  theme(legend.position = "bottom")

# -*- coding: utf-8 -*-
"""
Created on Thu Sep 10 14:31:32 2026

@author: alhamwi
"""

import pandas as pd
import matplotlib.pyplot as plt

# ============================================================
# 1. LOAD DATA
# ============================================================

file_path = r"C:\Users\alhamwi\Desktop\NDVI_Labonwheels.xlsx"

df = pd.read_excel(file_path)

# Rename first column
df = df.rename(columns={df.columns[0]: "Material"})

# Convert decimal commas if needed
for col in ["High cost", "Low cost"]:
    df[col] = (
        df[col]
        .astype(str)
        .str.replace(",", ".", regex=False)
    )
    df[col] = pd.to_numeric(df[col], errors="coerce")


# ============================================================
# 2. CREATE 1:1 PLOT
# ============================================================

fig, ax = plt.subplots(figsize=(7, 7))

ax.scatter(
    df["High cost"],
    df["Low cost"],
    s=80
)

# Add material labels
for _, row in df.iterrows():
    ax.annotate(
        row["Material"],
        (row["High cost"], row["Low cost"]),
        xytext=(6, 6),
        textcoords="offset points",
        fontsize=10
    )


# ============================================================
# 3. ADD 1:1 LINE
# ============================================================

ax.plot(
    [0, 1],
    [0, 1],
    linestyle="--",
    linewidth=1.5,
    label="1:1 line"
)


# ============================================================
# 4. FORMAT
# ============================================================

ax.set_xlim(0, 1)
ax.set_ylim(0, 1)

# Makes x and y units physically identical
ax.set_aspect("equal", adjustable="box")

ax.set_xlabel("High-cost NDVI", fontsize=12)
ax.set_ylabel("Low-cost NDVI", fontsize=12)

ax.set_xticks([0, 0.2, 0.4, 0.6, 0.8, 1.0])
ax.set_yticks([0, 0.2, 0.4, 0.6, 0.8, 1.0])

ax.legend(frameon=False)

ax.spines["top"].set_visible(False)
ax.spines["right"].set_visible(False)

plt.tight_layout()
plt.show()

#%%

import pandas as pd
import matplotlib.pyplot as plt

# ============================================================
# SETTINGS
# ============================================================

file_path = r"C:\Users\alhamwi\Desktop\NDVI_Labonwheels.xlsx"

high_sheet = "SET1"
low_sheet = "Low cost"

ndvi_column = "NDVI"
group_column = "Standort+Plot"


# ============================================================
# 1. LOAD DATA
# ============================================================

high = pd.read_excel(file_path, sheet_name=high_sheet)
low = pd.read_excel(file_path, sheet_name=low_sheet)

# Remove accidental spaces from column names
high.columns = high.columns.str.strip()
low.columns = low.columns.str.strip()

print("High-cost columns:")
print(high.columns.tolist())

print("\nLow-cost columns:")
print(low.columns.tolist())


# ============================================================
# 2. CLEAN Standort+Plot FOR MATCHING
# ============================================================

# Keep original values, but create a matching key.
# This makes matching case-insensitive.
#
# Example:
# "ZALF+Black" = "zalf+black"

high["match_key"] = (
    high[group_column]
    .astype(str)
    .str.strip()
    .str.casefold()
)

low["match_key"] = (
    low[group_column]
    .astype(str)
    .str.strip()
    .str.casefold()
)


# ============================================================
# 3. CONVERT NDVI TO NUMERIC
# ============================================================

for df in [high, low]:

    df[ndvi_column] = (
        df[ndvi_column]
        .astype(str)
        .str.replace(",", ".", regex=False)
    )

    df[ndvi_column] = pd.to_numeric(
        df[ndvi_column],
        errors="coerce"
    )


# ============================================================
# 4. GROUP HIGH-COST DATA
# ============================================================

high_grouped = (
    high
    .groupby("match_key", as_index=False)
    .agg(
        High_mean=(ndvi_column, "mean"),
        High_SD=(ndvi_column, "std"),
        High_n=(ndvi_column, "count")
    )
)


# ============================================================
# 5. GROUP LOW-COST DATA
# ============================================================

low_grouped = (
    low
    .groupby("match_key", as_index=False)
    .agg(
        Low_mean=(ndvi_column, "mean"),
        Low_SD=(ndvi_column, "std"),
        Low_n=(ndvi_column, "count")
    )
)


# ============================================================
# 6. MERGE BASED ON Standort+Plot
# ============================================================

merged = pd.merge(
    high_grouped,
    low_grouped,
    on="match_key",
    how="inner"
)


# ============================================================
# 7. USE ORIGINAL Standort+Plot NAME AS LABEL
# ============================================================

labels = (
    high[["match_key", group_column]]
    .drop_duplicates("match_key")
)

merged = merged.merge(
    labels,
    on="match_key",
    how="left"
)


# ============================================================
# 8. SHOW RESULTS
# ============================================================

print("\nGrouped and merged results:\n")

print(
    merged[
        [
            group_column,
            "High_mean",
            "High_SD",
            "High_n",
            "Low_mean",
            "Low_SD",
            "Low_n"
        ]
    ].to_string(index=False)
)


# ============================================================
# 9. 1:1 PLOT WITH STANDARD DEVIATION
# ============================================================

fig, ax = plt.subplots(figsize=(8, 8))

ax.errorbar(
    merged["High_mean"],
    merged["Low_mean"],

    xerr=merged["High_SD"],
    yerr=merged["Low_SD"],

    fmt="o",
    markersize=7,

    capsize=4,
    elinewidth=1.2,
    capthick=1.2,

    linestyle="none"
)


# ============================================================
# 10. LABEL EACH POINT
# ============================================================

for _, row in merged.iterrows():

    ax.annotate(
        row[group_column],
        (
            row["High_mean"],
            row["Low_mean"]
        ),
        xytext=(6, 6),
        textcoords="offset points",
        fontsize=8
    )


# ============================================================
# 11. 1:1 LINE
# ============================================================

ax.plot(
    [0, 1],
    [0, 1],
    linestyle="--",
    linewidth=1.5,
    label="1:1 line"
)


# ============================================================
# 12. AXES
# ============================================================

ax.set_xlim(0, 1)
ax.set_ylim(0, 1)

ax.set_aspect(
    "equal",
    adjustable="box"
)

ax.set_xlabel("High-cost NDVI", fontsize=12)
ax.set_ylabel("Low-cost NDVI", fontsize=12)

ax.set_xticks([0, 0.2, 0.4, 0.6, 0.8, 1.0])
ax.set_yticks([0, 0.2, 0.4, 0.6, 0.8, 1.0])

ax.legend(frameon=False)

ax.spines["top"].set_visible(False)
ax.spines["right"].set_visible(False)

plt.tight_layout()
plt.show()


# ============================================================
# 13. SAVE RESULTS
# ============================================================

output_file = r"C:\Users\alhamwi\Desktop\NDVI_grouped_comparison.xlsx"

merged.to_excel(
    output_file,
    index=False
)

print("\nSaved to:")
print(output_file)
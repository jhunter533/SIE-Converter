import glob
import os

import numpy as np
import pandas as pd
import pyarrow as pa
import pyarrow.parquet as pq

INPUT_DIR="/mnt/hdd_mount/Bergey_Wind_Soil_Pile_Tower_Interaction/SIE Files/CSV/"
OUTPUT_DIR="/mnt/hdd_mount/Bergey_Wind_Soil_Pile_Tower_Interaction/SIE Files/Parquet/"
chunk_size = 100000

# cursed solution to move past the 20 problematic ones
def make_unique(columns):
    seen = {}
    new_cols = []
    for col in columns:
        if col not in seen:
            seen[col] = 0
            new_cols.append(col)
        else:
            seen[col] += 1
            new_cols.append(f"{col}_dup{seen[col]}")
    return new_cols
def to_float_safe(df):
    for col in df.columns:
        df[col]=pd.to_numeric(df[col],errors='coerce').astype(float)
    return df

os.makedirs(OUTPUT_DIR,exist_ok=True)
csv_files=glob.glob(os.path.join(INPUT_DIR,"*.csv"))

for csv_path in csv_files:
    base_name=os.path.splitext(os.path.basename(csv_path))[0]
    parquet_path=os.path.join(OUTPUT_DIR,base_name+".parquet")
    if os.path.exists(parquet_path):
        print(f"Skipping: {parquet_path}")
        continue
    print(f"Processing: {csv_path}->{parquet_path}")


    first_rows=pd.read_csv(csv_path,nrows=3,header=None)
    col_names=first_rows.iloc[1].values
    units_row=first_rows.iloc[2].values
    col_names=make_unique(col_names)

    reader = pd.read_csv(csv_path,skiprows=3,header=None, chunksize=chunk_size,low_memory=False,dtype=str)

    first_chunk = next(reader)
    if first_chunk.shape[1]<len(col_names):
        for i in range(first_chunk.shape[1],len(col_names)):
            first_chunk[i]=np.nan
    elif first_chunk.shape[1]>len(col_names):
        first_chunk=first_chunk.iloc[:,:len(col_names)]

    first_chunk.columns=col_names
    first_chunk=to_float_safe(first_chunk)
    table=pa.Table.from_pandas(first_chunk)
    metadata={b"units":",".join(units_row).encode("utf-8")}

    writer = pq.ParquetWriter(parquet_path, table.schema)
    writer.add_key_value_metadata(metadata)

    writer.write_table(table)
    
    for chunk in reader:
        if chunk.shape[1]<len(col_names):
            for i in range(chunk.shape[1],len(col_names)):
                chunk[i]=np.nan
        elif chunk.shape[1]>len(col_names):
            chunk=chunk.iloc[:,:len(col_names)]
    
        chunk.columns=col_names
        chunk=to_float_safe(chunk)
        writer.write_table(pa.Table.from_pandas(chunk))

    writer.close()
    print(f"Converted successfully")
print("All conversions done")

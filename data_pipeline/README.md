# Data Pipeline

The project does not ship with pre-built map data. Users generate the graph themselves using the extraction script.

## Generating the Graph

Run the pipeline from the project root:

```bash
/usr/bin/python3 data_pipeline/extract_osm.py
```

### What happens when you run this:
1. The script queries the OpenStreetMap API using the IIT Jodhpur bounding box.
2. It downloads the raw map XML to `data/iitj.osm`.
3. It parses all highway nodes and road ways.
4. It extracts the largest connected component to ensure all nodes have valid routes.
5. It exports the clean graph to `data/iitj_graph.json`.

After the file `data/iitj_graph.json` is generated, you can feed it into the C++ engine:

```bash
cd cpp-engine
make
./bin/pathos_engine ../data/iitj_graph.json
```

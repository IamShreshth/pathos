#!/usr/bin/env python3
"""
IIT Jodhpur Road Network Extractor for Pathos-X
Extracts nodes and edges from OpenStreetMap and exports a clean, connected JSON graph.
"""

import json
import math
import os
import urllib.request
import xml.etree.ElementTree as ET

# Bounding box covering IIT Jodhpur campus: (min_lat, min_lon, max_lat, max_lon)
IITJ_BBOX = (26.4597, 73.1079, 26.4867, 73.1254)


def haversine(lat1, lon1, lat2, lon2):
    """Calculates great-circle distance between two points on Earth in meters."""
    R = 6371000.0
    phi1 = math.radians(lat1)
    phi2 = math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlambda = math.radians(lon2 - lon1)

    a = math.sin(dphi / 2.0) ** 2 + math.cos(phi1) * math.cos(phi2) * math.sin(dlambda / 2.0) ** 2
    c = 2.0 * math.atan2(math.sqrt(a), math.sqrt(1.0 - a))
    return round(R * c, 2)


def download_osm_data(bbox, output_file):
    """Downloads OSM XML directly from the OpenStreetMap API."""
    min_lat, min_lon, max_lat, max_lon = bbox
    url = f"https://api.openstreetmap.org/api/0.6/map?bbox={min_lon},{min_lat},{max_lon},{max_lat}"
    headers = {"User-Agent": "PathosEduGraphExtractor/1.0"}

    print("[1/4] Fetching OSM data from OpenStreetMap API...")
    req = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(req) as response:
        content = response.read()
        with open(output_file, "wb") as f:
            f.write(content)
    print(f"      Downloaded {len(content):,} bytes -> {output_file}")


def parse_osm_and_build_graph(osm_file):
    """Parses raw OSM XML, extracts connected road network, and returns clean graph."""
    print("[2/4] Parsing OSM XML data...")
    tree = ET.parse(osm_file)
    root = tree.getroot()

    # 1. Parse node coordinates
    raw_nodes = {}
    for node in root.findall("node"):
        nid = int(node.attrib["id"])
        lat = float(node.attrib["lat"])
        lon = float(node.attrib["lon"])
        raw_nodes[nid] = (lat, lon)

    # 2. Parse road ways
    valid_highways = {
        "primary", "secondary", "tertiary", "residential", "service",
        "unclassified", "living_street", "footway", "path", "cycleway", "pedestrian"
    }

    raw_edges = []
    adj_temp = {}

    for way in root.findall("way"):
        highway_type = None
        oneway = False
        name = ""

        for tag in way.findall("tag"):
            k = tag.attrib.get("k")
            v = tag.attrib.get("v")
            if k == "highway":
                highway_type = v
            elif k == "oneway" and v in ("yes", "1", "true"):
                oneway = True
            elif k == "name":
                name = v

        if highway_type in valid_highways:
            refs = [int(nd.attrib["ref"]) for nd in way.findall("nd")]
            for i in range(len(refs) - 1):
                u, v = refs[i], refs[i + 1]
                if u in raw_nodes and v in raw_nodes:
                    raw_edges.append((u, v, oneway, name))
                    adj_temp.setdefault(u, []).append(v)
                    adj_temp.setdefault(v, []).append(u)

    # 3. Extract the largest connected component so all routes are reachable
    print("[3/4] Keeping the largest connected component...")
    visited = set()
    components = []
    for nid in adj_temp:
        if nid not in visited:
            comp = []
            queue = [nid]
            visited.add(nid)
            for curr in queue:
                comp.append(curr)
                for neighbor in adj_temp.get(curr, []):
                    if neighbor not in visited:
                        visited.add(neighbor)
                        queue.append(neighbor)
            components.append(comp)

    components.sort(key=len, reverse=True)
    largest_comp = set(components[0])
    print(f"      Total components: {len(components)}, Largest: {len(largest_comp)} nodes")

    # 4. Map retained OSM nodes to continuous 0-indexed IDs
    osm_to_internal = {}
    nodes_list = []
    for internal_id, osm_id in enumerate(sorted(largest_comp)):
        osm_to_internal[osm_id] = internal_id
        lat, lon = raw_nodes[osm_id]
        nodes_list.append({
            "id": internal_id,
            "osm_id": osm_id,
            "lat": lat,
            "lon": lon
        })

    # 5. Build directed/bidirectional edges
    edges_list = []
    for u_osm, v_osm, oneway, name in raw_edges:
        if u_osm in largest_comp and v_osm in largest_comp:
            u = osm_to_internal[u_osm]
            v = osm_to_internal[v_osm]
            lat1, lon1 = raw_nodes[u_osm]
            lat2, lon2 = raw_nodes[v_osm]
            length = haversine(lat1, lon1, lat2, lon2)

            edges_list.append({
                "u": u,
                "v": v,
                "length": length,
                "name": name,
                "oneway": oneway
            })

            if not oneway:
                edges_list.append({
                    "u": v,
                    "v": u,
                    "length": length,
                    "name": name,
                    "oneway": oneway
                })

    return {
        "metadata": {
            "name": "IIT Jodhpur Campus Road Network",
            "bbox": IITJ_BBOX,
            "node_count": len(nodes_list),
            "edge_count": len(edges_list)
        },
        "nodes": nodes_list,
        "edges": edges_list
    }


def main():
    os.makedirs("data", exist_ok=True)
    osm_path = "data/iitj.osm"
    json_path = "data/iitj_graph.json"

    if not os.path.exists(osm_path):
        download_osm_data(IITJ_BBOX, osm_path)
    else:
        print(f"[1/4] Found existing OSM file at {osm_path}")

    graph_data = parse_osm_and_build_graph(osm_path)

    print(f"[4/4] Writing clean graph to {json_path}...")
    with open(json_path, "w") as f:
        json.dump(graph_data, f, indent=2)

    meta = graph_data["metadata"]
    print("\n[SUCCESS] Graph generation complete!")
    print(f"          Connected Nodes: {meta['node_count']}")
    print(f"          Edges: {meta['edge_count']}")
    print(f"          Saved to: {json_path}")


if __name__ == "__main__":
    main()

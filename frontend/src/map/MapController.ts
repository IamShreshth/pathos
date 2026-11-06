// Imperative wrapper around Leaflet: markers, route layers and search animation.
import L from "leaflet";
import type { Coordinate, GraphMetadata, LineString, SnappedPoint } from "../api/types";

export interface AnimationRun {
  color: string;
  segments: [number, number, number, number][]; // [parentLat, parentLon, lat, lon]
  onDone?: () => void;
}

const toLatLngs = (g: LineString): L.LatLngExpression[] => g.coordinates.map(([lon, lat]) => [lat, lon]);

function pinIcon(label: string, color: string): L.DivIcon {
  return L.divIcon({
    className: "pin",
    html: `<div class="pin-body" style="background:${color}">${label}</div>`,
    iconSize: [26, 26],
    iconAnchor: [13, 13],
  });
}

export class MapController {
  readonly map: L.Map;
  private readonly canvas = L.canvas({ padding: 0.5 });
  private readonly coverage = L.layerGroup();
  private readonly search = L.layerGroup();
  private readonly casings = L.layerGroup(); // white outlines, always below every route line
  private readonly routes = L.layerGroup();
  private readonly markers = L.layerGroup();
  private readonly proposal = L.layerGroup(); // preview of an area that could be downloaded
  private frame = 0;

  constructor(el: HTMLElement, onClick: (c: Coordinate) => void) {
    this.map = L.map(el, { preferCanvas: true, zoomControl: true }).setView([26.9124, 75.7873], 14);
    // Standard OSM tiles (fine for development/demo use under the OSM tile usage policy).
    L.tileLayer("https://tile.openstreetmap.org/{z}/{x}/{y}.png", {
      maxZoom: 19,
      className: "base-tiles",
      attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors',
    }).addTo(this.map);
    // Separate panes give casings and routes their own canvas, so every casing stays
    // below every route line regardless of drawing order.
    this.map.createPane("casings").style.zIndex = "410";
    this.map.createPane("routes").style.zIndex = "420";
    for (const g of [this.coverage, this.proposal, this.search, this.casings, this.routes, this.markers]) g.addTo(this.map);
    this.map.on("click", (e: L.LeafletMouseEvent) => onClick({ lat: e.latlng.lat, lon: e.latlng.lng }));
  }

  destroy(): void {
    this.stopAnimation();
    this.map.remove();
  }

  showCoverage(meta: GraphMetadata): void {
    const b = meta.bbox;
    const bounds = L.latLngBounds([b.south, b.west], [b.north, b.east]);
    this.coverage.clearLayers();
    L.rectangle(bounds, { color: "#495057", weight: 1, dashArray: "6 6", fill: false, interactive: false }).addTo(
      this.coverage,
    );
    this.map.fitBounds(bounds.pad(0.02));
  }

  /** Current centre of the visible map. */
  center(): Coordinate {
    const c = this.map.getCenter();
    return { lat: c.lat, lon: c.lng };
  }

  /** Dashed circle showing the area that "Load roads" would download, plus the clicked point. */
  previewArea(center: Coordinate | null, radiusM = 0, point?: Coordinate): void {
    this.proposal.clearLayers();
    if (!center) return;
    const circle = L.circle([center.lat, center.lon], {
      radius: radiusM,
      color: "#e67700",
      weight: 2,
      dashArray: "8 6",
      fillColor: "#ffd43b",
      fillOpacity: 0.08,
      interactive: false,
    }).addTo(this.proposal);
    if (point)
      L.circleMarker([point.lat, point.lon], { radius: 7, color: "#e67700", weight: 3, fillColor: "#fff", fillOpacity: 1 }).addTo(
        this.proposal,
      );
    this.map.fitBounds(circle.getBounds().pad(0.1));
  }

  /** Remove everything drawn for the previous graph. */
  clearAll(): void {
    this.clearSearch();
    this.clearRoutes();
    this.clearMarkers();
    this.proposal.clearLayers();
  }

  // ---------------------------------------------------------------- markers
  clearMarkers(): void {
    this.markers.clearLayers();
  }

  addPin(at: Coordinate, label: string, color: string, tooltip?: string): void {
    const m = L.marker([at.lat, at.lon], { icon: pinIcon(label, color), keyboard: false }).addTo(this.markers);
    if (tooltip) m.bindTooltip(tooltip, { direction: "top", offset: [0, -12] });
  }

  /** Small dot on the road node a click was snapped to, joined to the click by a dashed line. */
  addSnap(p: SnappedPoint, color: string): void {
    L.polyline(
      [
        [p.input.lat, p.input.lon],
        [p.lat, p.lon],
      ],
      { color, weight: 1.5, dashArray: "3 4", interactive: false },
    ).addTo(this.markers);
    L.circleMarker([p.lat, p.lon], { radius: 4, color, weight: 2, fillColor: "#fff", fillOpacity: 1 })
      .bindTooltip(`snapped to node ${p.node_id} (${p.snap_distance_m.toFixed(0)} m)`)
      .addTo(this.markers);
  }

  // ----------------------------------------------------------------- routes
  clearRoutes(): void {
    this.casings.clearLayers();
    this.routes.clearLayers();
  }

  /** `width` lets overlapping routes be stacked: draw wide ones first, narrow ones on top. */
  drawRoute(g: LineString, color: string, opts: { width?: number; tooltip?: string; dashed?: boolean } = {}): L.Polyline {
    const width = opts.width ?? 5;
    // A white casing underneath keeps routes readable over the search trace.
    L.polyline(toLatLngs(g), { pane: "casings", color: "#fff", weight: width + 3, opacity: 0.9, interactive: false }).addTo(this.casings);
    const line = L.polyline(toLatLngs(g), {
      pane: "routes",
      color,
      weight: width,
      opacity: 0.95,
      dashArray: opts.dashed ? "8 8" : undefined,
    }).addTo(this.routes);
    if (opts.tooltip) line.bindTooltip(opts.tooltip, { sticky: true });
    return line;
  }

  fitTo(lines: LineString[]): void {
    const pts = lines.flatMap(toLatLngs);
    if (pts.length) this.map.fitBounds(L.latLngBounds(pts as L.LatLngTuple[]).pad(0.15));
  }

  // ------------------------------------------------------------- animation
  clearSearch(): void {
    this.stopAnimation();
    this.search.clearLayers();
  }

  /** Dim the explored-edge layer so the final routes stand out. */
  fadeSearch(opacity = 0.18): void {
    this.search.eachLayer((l) => (l as L.Polyline).setStyle({ opacity }));
  }

  stopAnimation(): void {
    if (this.frame) cancelAnimationFrame(this.frame);
    this.frame = 0;
  }

  /**
   * Replays recorded search events. Every run advances by the same number of
   * expanded nodes per frame, so algorithms that expand fewer nodes finish first.
   */
  animate(
    runs: AnimationRun[],
    eventsPerFrame: number,
    onProgress: (done: number[]) => void,
    onAllDone?: () => void,
  ): void {
    this.clearSearch();
    const pos = runs.map(() => 0);
    const finished = runs.map(() => false);

    const step = () => {
      let active = false;
      runs.forEach((run, i) => {
        if (finished[i]) return;
        const end = Math.min(run.segments.length, pos[i] + eventsPerFrame);
        if (end > pos[i]) {
          const batch: L.LatLngExpression[][] = [];
          for (let k = pos[i]; k < end; k++) {
            const [pl, po, l, o] = run.segments[k];
            batch.push([
              [pl, po],
              [l, o],
            ]);
          }
          L.polyline(batch, {
            renderer: this.canvas,
            color: run.color,
            weight: 2,
            opacity: 0.45,
            interactive: false,
          }).addTo(this.search);
          pos[i] = end;
        }
        if (pos[i] >= run.segments.length) {
          finished[i] = true;
          run.onDone?.();
        } else {
          active = true;
        }
      });
      onProgress([...pos]);
      this.frame = active ? requestAnimationFrame(step) : 0;
      if (!active) onAllDone?.();
    };
    this.frame = requestAnimationFrame(step);
  }
}

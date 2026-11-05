import { useEffect, useRef } from "react";
import "leaflet/dist/leaflet.css";
import type { Coordinate } from "../api/types";
import { MapController } from "./MapController";

interface Props {
  onReady: (c: MapController) => void;
  onClick: (c: Coordinate) => void;
}

export function MapView({ onReady, onClick }: Props) {
  const el = useRef<HTMLDivElement>(null);
  // Keep the latest click handler without re-creating the map.
  const clickRef = useRef(onClick);
  clickRef.current = onClick;
  const readyRef = useRef(onReady);
  readyRef.current = onReady;

  useEffect(() => {
    if (!el.current) return;
    const ctrl = new MapController(el.current, (c) => clickRef.current(c));
    readyRef.current(ctrl);
    return () => ctrl.destroy();
  }, []);

  return <div ref={el} className="map" />;
}

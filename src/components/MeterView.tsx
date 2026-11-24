import React, { useEffect, useRef } from 'react';
import { clamp, formatDb } from '../audio/curves';
import { MeterMode, MonitorSnapshot } from '../audio/types';

interface MeterViewProps {
  mode: MeterMode;
  onCycleMode: () => void;
  getSnapshot: () => MonitorSnapshot;
}

export const MeterView: React.FC<MeterViewProps> = ({
  mode,
  onCycleMode,
  getSnapshot,
}) => {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const containerRef = useRef<HTMLDivElement | null>(null);
  const modeRef = useRef(mode);
  modeRef.current = mode;

  useEffect(() => {
    let rafId = 0;
    const names = ['PEAK', 'RMS', 'CREST'];

    const draw = () => {
      const canvas = canvasRef.current;
      const container = containerRef.current;
      if (canvas && container) {
        const rect = container.getBoundingClientRect();
        const dpr = window.devicePixelRatio || 1;
        const w = Math.max(1, Math.floor(rect.width));
        const h = Math.max(1, Math.floor(rect.height));

        if (canvas.width !== Math.floor(w * dpr) || canvas.height !== Math.floor(h * dpr)) {
          canvas.width = Math.floor(w * dpr);
          canvas.height = Math.floor(h * dpr);
        }

        const ctx = canvas.getContext('2d');
        if (ctx) {
          const m = modeRef.current;
          const s = getSnapshot();

          ctx.save();
          ctx.scale(dpr, dpr);

          ctx.fillStyle = '#7b7b7b';
          ctx.beginPath();
          ctx.roundRect(0.5, 0.5, w - 1, h - 1, 4);
          ctx.fill();
          ctx.strokeStyle = '#3a3a3a';
          ctx.lineWidth = 1;
          ctx.stroke();

          let now = 0;
          let hold = 0;
          let lo = -60;
          let hi = 0;
          if (m === 0) {
            now = s.peakNow;
            hold = s.peakHold;
          } else if (m === 1) {
            now = s.rmsNow;
            hold = s.rmsHold;
          } else {
            now = s.crestNow;
            hold = s.crestHold;
            lo = 0;
            hi = 24;
          }

          const toDb = (v: number) => 20 * Math.log10(Math.max(v, 1e-6));
          const nowDb = toDb(now);
          const holdDb = toDb(hold);
          const norm = (db: number) => clamp((db - lo) / (hi - lo), 0, 1);

          ctx.fillStyle = '#181818';
          ctx.font = 'bold 11px -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif';
          ctx.textAlign = 'center';
          ctx.textBaseline = 'middle';
          ctx.fillText(names[m], w * 0.5, 14);

          const barX = Math.round(w * 0.3);
          const barW = Math.max(8, w - barX * 2);
          const barY = 26;
          const barH = Math.max(20, h - 72);

          ctx.fillStyle = '#252525';
          ctx.beginPath();
          ctx.roundRect(barX, barY, barW, barH, 2);
          ctx.fill();

          const levelH = barH * norm(nowDb);
          if (levelH > 0.5) {
            const grad = ctx.createLinearGradient(0, barY, 0, barY + barH);
            grad.addColorStop(0, '#ff4e42');
            grad.addColorStop(0.25, '#ffd24a');
            grad.addColorStop(1, '#58e058');
            ctx.fillStyle = grad;
            ctx.beginPath();
            ctx.roundRect(barX, barY + barH - levelH, barW, levelH, 2);
            ctx.fill();
          }

          if (hold > 0) {
            const hy = barY + barH - barH * norm(holdDb);
            ctx.fillStyle = '#ffffff';
            ctx.fillRect(barX - 3, hy - 1, barW + 6, 2);
          }

          ctx.fillStyle = '#111111';
          ctx.font = 'bold 11px monospace';
          const nowText = m === 2 ? `${nowDb.toFixed(1)}` : formatDb(now);
          ctx.fillText(nowText, w * 0.5, h - 28);

          ctx.fillStyle = '#262626';
          ctx.font = '10px monospace';
          const holdText = m === 2 ? `${holdDb.toFixed(1)}` : formatDb(hold);
          ctx.fillText(holdText, w * 0.5, h - 13);

          ctx.restore();
        }
      }
      rafId = requestAnimationFrame(draw);
    };

    rafId = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(rafId);
  }, [getSnapshot]);

  return (
    <div
      ref={containerRef}
      onClick={onCycleMode}
      title="Click to cycle Peak / RMS / Crest meter"
      className="w-[64px] h-full shrink-0 cursor-pointer"
    >
      <canvas ref={canvasRef} className="block w-full h-full rounded-[4px]" />
    </div>
  );
};

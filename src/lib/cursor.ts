// The caret's line and column, for the status bar only. Kept out of the app store so moving the
// cursor (every keystroke) redraws one small label, not the whole window.
import { create } from "zustand";

export const useCursor = create<{ line: number; col: number }>(() => ({ line: 1, col: 1 }));

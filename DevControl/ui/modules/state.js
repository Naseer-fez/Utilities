// App state management module
export const state = {
    ws: null,
    telemetryChart: null,
    networkChart: null,
    historyChart: null,
    chartData: {
        labels: [],
        cpu: [],
        ram: [],
        gpu: [],
        netDown: [],
        netUp: []
    },
    activeTab: 'overview',
    logs: [],
    logFilter: 'ALL',
    processes: [],
    apiIntervals: []
};

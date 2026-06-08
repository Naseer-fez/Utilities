use chrono::Local;
use super::sqlite_logger::SqliteLogger;

pub struct Analytics {
    logger: std::sync::Arc<SqliteLogger>,
}

impl Analytics {
    pub fn new(logger: std::sync::Arc<SqliteLogger>) -> Self {
        Self { logger }
    }

    /// Helper to format durations into human readable strings like "1h 15m" or "45s"
    pub fn format_duration(seconds: u64) -> String {
        if seconds == 0 {
            return "0s".to_string();
        }
        
        let hours = seconds / 3600;
        let minutes = (seconds % 3600) / 60;
        let secs = seconds % 60;
        
        if hours > 0 {
            format!("{}h {}m", hours, minutes)
        } else if minutes > 0 {
            format!("{}m {}s", minutes, secs)
        } else {
            format!("{}s", secs)
        }
    }

    /// Retrieves and formats today's usage statistics for display in the tray menu,
    /// merging active in-memory session cache and ensuring all tracked apps are displayed.
    pub fn get_today_stats_formatted(
        &self,
        active_sessions: Vec<(String, u64, u64)>,
        tracked_apps: &[String],
    ) -> Vec<(String, String)> {
        let today = Local::now().date_naive();
        let db_stats = self.logger.get_daily_stats(today).unwrap_or_default();
        
        let mut merged: std::collections::HashMap<String, (u64, u64)> = std::collections::HashMap::new();
        let tracked_set: std::collections::HashSet<String> = tracked_apps.iter()
            .map(|s| s.to_lowercase())
            .collect();
        
        // Initialize all tracked apps with 0 values so they are always visible in the menu
        for app in tracked_apps {
            merged.insert(app.to_lowercase(), (0, 0));
        }
        
        // Insert database stats (only if they are in the tracked list)
        for (exe, total, fg) in db_stats {
            let exe_lower = exe.to_lowercase();
            if tracked_set.contains(&exe_lower) {
                let entry = merged.entry(exe_lower).or_insert((0, 0));
                entry.0 += total;
                entry.1 += fg;
            }
        }
        
        // Add/merge active session stats (only if they are in the tracked list)
        for (exe, total, fg) in active_sessions {
            let exe_lower = exe.to_lowercase();
            if tracked_set.contains(&exe_lower) {
                let entry = merged.entry(exe_lower).or_insert((0, 0));
                entry.0 += total;
                entry.1 += fg;
            }
        }
        
        // Convert to sorted vector (by active focus time descending, then total time descending)
        let mut sorted: Vec<(String, u64, u64)> = merged.into_iter()
            .map(|(exe, (total, fg))| (exe, total, fg))
            .collect();
        sorted.sort_by(|a, b| {
            b.2.cmp(&a.2).then_with(|| b.0.cmp(&a.0))
        });
        
        // Format for display
        sorted.into_iter().map(|(exe, total, fg)| {
            let total_str = Self::format_duration(total);
            let fg_str = Self::format_duration(fg);
            let display_str = format!("Total: {} | Focus: {}", total_str, fg_str);
            (exe, display_str)
        }).collect()
    }

    /// Generates a clean, modern HTML report page for today's application statistics
    pub fn generate_report_html(
        &self,
        active_sessions: Vec<(String, u64, u64)>,
        tracked_apps: &[String],
    ) -> String {
        let today = Local::now().date_naive();
        let db_stats = self.logger.get_daily_stats(today).unwrap_or_default();
        
        let mut merged: std::collections::HashMap<String, (u64, u64)> = std::collections::HashMap::new();
        let tracked_set: std::collections::HashSet<String> = tracked_apps.iter()
            .map(|s| s.to_lowercase())
            .collect();
        
        for app in tracked_apps {
            merged.insert(app.to_lowercase(), (0, 0));
        }
        for (exe, total, fg) in db_stats {
            let exe_lower = exe.to_lowercase();
            if tracked_set.contains(&exe_lower) {
                let entry = merged.entry(exe_lower).or_insert((0, 0));
                entry.0 += total;
                entry.1 += fg;
            }
        }
        for (exe, total, fg) in active_sessions {
            let exe_lower = exe.to_lowercase();
            if tracked_set.contains(&exe_lower) {
                let entry = merged.entry(exe_lower).or_insert((0, 0));
                entry.0 += total;
                entry.1 += fg;
            }
        }

        let mut sorted: Vec<(String, u64, u64)> = merged.into_iter()
            .map(|(exe, (total, fg))| (exe, total, fg))
            .collect();
        sorted.sort_by(|a, b| b.2.cmp(&a.2).then_with(|| b.0.cmp(&a.0)));

        let mut rows = String::new();
        let mut total_tracked_s = 0;
        let mut total_active_s = 0;
        let mut most_used_app = "None".to_string();
        let mut max_active = 0;

        for (exe, total, fg) in &sorted {
            total_tracked_s += *total;
            total_active_s += *fg;
            if *fg > max_active {
                max_active = *fg;
                most_used_app = exe.clone();
            }

            let total_str = Self::format_duration(*total);
            let fg_str = Self::format_duration(*fg);
            let percent = (*fg * 100).checked_div(*total).unwrap_or(0);

            rows.push_str(&format!(
                r#"<tr>
                    <td class="app-name">{}</td>
                    <td>{}</td>
                    <td>{}</td>
                    <td>
                        <div class="progress-bg">
                            <div class="progress-bar" style="width: {}%;"></div>
                        </div>
                        <span class="percent-label">{}% Active</span>
                    </td>
                </tr>"#,
                exe, total_str, fg_str, percent, percent
            ));
        }

        let summary_total_tracked = Self::format_duration(total_tracked_s);
        let summary_total_active = Self::format_duration(total_active_s);

        format!(
            r#"<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Daily Habit Tracker Report</title>
    <style>
        body {{
            background-color: #0b0f19;
            color: #f1f5f9;
            font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            margin: 0;
            padding: 40px 20px;
            display: flex;
            justify-content: center;
        }}
        .container {{
            max-width: 800px;
            width: 100%;
        }}
        header {{
            display: flex;
            justify-content: space-between;
            align-items: center;
            border-bottom: 1px solid #1e293b;
            padding-bottom: 20px;
            margin-bottom: 30px;
        }}
        h1 {{
            color: #10b981;
            margin: 0;
            font-size: 28px;
            font-weight: 800;
            letter-spacing: -0.025em;
        }}
        .date {{
            color: #64748b;
            font-size: 14px;
        }}
        .cards {{
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
            gap: 20px;
            margin-bottom: 40px;
        }}
        .card {{
            background-color: #111827;
            padding: 24px;
            border-radius: 16px;
            border: 1px solid #1f2937;
            box-shadow: 0 4px 6px -1px rgb(0 0 0 / 0.1), 0 2px 4px -2px rgb(0 0 0 / 0.1);
        }}
        .card-title {{
            color: #94a3b8;
            font-size: 14px;
            font-weight: 500;
            margin-bottom: 10px;
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }}
        .card-value {{
            font-size: 26px;
            font-weight: 700;
            color: #f8fafc;
        }}
        table {{
            width: 100%;
            border-collapse: collapse;
            background-color: #111827;
            border-radius: 16px;
            overflow: hidden;
            border: 1px solid #1f2937;
            box-shadow: 0 4px 6px -1px rgb(0 0 0 / 0.1);
        }}
        th, td {{
            padding: 18px 24px;
            text-align: left;
        }}
        th {{
            background-color: #0b0f19;
            color: #94a3b8;
            font-size: 12px;
            font-weight: 600;
            text-transform: uppercase;
            letter-spacing: 0.05em;
            border-bottom: 1px solid #1f2937;
        }}
        tr {{
            border-bottom: 1px solid #1f2937;
        }}
        tr:last-child {{
            border-bottom: none;
        }}
        .app-name {{
            font-weight: 600;
            color: #34d399;
        }}
        .progress-bg {{
            background-color: #1f2937;
            border-radius: 9999px;
            height: 8px;
            width: 150px;
            overflow: hidden;
            display: inline-block;
            vertical-align: middle;
            margin-right: 10px;
        }}
        .progress-bar {{
            background-color: #10b981;
            height: 100%;
            border-radius: 9999px;
        }}
        .percent-label {{
            font-size: 12px;
            color: #94a3b8;
            font-weight: 500;
        }}
    </style>
</head>
<body>
    <div class="container">
        <header>
            <div>
                <h1>Daily Usage Report</h1>
                <p style="margin: 5px 0 0 0; color: #94a3b8;">Habit Tracker Activity Log</p>
            </div>
            <div class="date">Generated: {}</div>
        </header>
        
        <div class="cards">
            <div class="card">
                <div class="card-title">Total Tracked</div>
                <div class="card-value">{}</div>
            </div>
            <div class="card">
                <div class="card-title">Active Focus Time</div>
                <div class="card-value" style="color: #10b981;">{}</div>
            </div>
            <div class="card">
                <div class="card-title">Most Active App</div>
                <div class="card-value" style="font-size: 20px; color: #f59e0b; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;">{}</div>
            </div>
        </div>

        <table>
            <thead>
                <tr>
                    <th>Application</th>
                    <th>Total Open Time</th>
                    <th>Active Focus Time</th>
                    <th>Engagement Ratio</th>
                </tr>
            </thead>
            <tbody>
                {}
            </tbody>
        </table>
    </div>
</body>
</html>"#,
            Local::now().format("%Y-%m-%d %H:%M:%S"),
            summary_total_tracked,
            summary_total_active,
            most_used_app,
            rows
        )
    }
}

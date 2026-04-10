-- Migration: Add user_role enum and role column to users table
-- Run this against an existing database (init.sql already includes these changes)

CREATE TYPE user_role AS ENUM ('admin', 'Stufenleiter', 'Leiter', 'Pio');

ALTER TABLE users
    ADD COLUMN role user_role NOT NULL DEFAULT 'Leiter';

-- Promote any user whose email contains "admin" to admin role
UPDATE users SET role = 'admin' WHERE email ILIKE '%admin%';

-- NppDev's sample database. Every run of a .sql file starts from a fresh copy of it, so your
-- INSERT / UPDATE / DELETE / DROP statements never change it for the next run.

-- ---------------------------------------------------------------- a small company
CREATE TABLE departments (
  dept_id   INTEGER PRIMARY KEY,
  name      TEXT NOT NULL,
  location  TEXT
);
INSERT INTO departments VALUES
  (10, 'Engineering', 'Bengaluru'),
  (20, 'Sales',       'Mumbai'),
  (30, 'Marketing',   'Chennai'),
  (40, 'Finance',     'Delhi'),
  (50, 'Support',     'Hyderabad');

CREATE TABLE employees (
  emp_id      INTEGER PRIMARY KEY,
  name        TEXT NOT NULL,
  email       TEXT UNIQUE,
  dept_id     INTEGER REFERENCES departments(dept_id),
  manager_id  INTEGER REFERENCES employees(emp_id),
  job_title   TEXT,
  salary      INTEGER,
  hire_date   TEXT
);
INSERT INTO employees VALUES
  (1,  'Asha Rao',        'asha@company.test',    10, NULL, 'CTO',                250000, '2015-03-01'),
  (2,  'Vikram Singh',    'vikram@company.test',  10, 1,    'Engineering Manager', 180000, '2017-07-15'),
  (3,  'Priya Nair',      'priya@company.test',   10, 2,    'Senior Engineer',     150000, '2018-01-10'),
  (4,  'Rahul Verma',     'rahul@company.test',   10, 2,    'Engineer',            110000, '2020-06-01'),
  (5,  'Sneha Iyer',      'sneha@company.test',   10, 2,    'Engineer',            105000, '2021-09-20'),
  (6,  'Arjun Mehta',     'arjun@company.test',   20, 1,    'Sales Head',          160000, '2016-11-11'),
  (7,  'Kavya Reddy',     'kavya@company.test',   20, 6,    'Sales Executive',      70000, '2019-04-05'),
  (8,  'Rohan Das',       'rohan@company.test',   20, 6,    'Sales Executive',      72000, '2022-02-14'),
  (9,  'Meera Pillai',    'meera@company.test',   30, 1,    'Marketing Lead',      120000, '2018-08-30'),
  (10, 'Karthik Raj',     'karthik@company.test', 30, 9,    'Content Writer',       60000, '2023-01-09'),
  (11, 'Divya Menon',     'divya@company.test',   40, 1,    'Finance Manager',     140000, '2017-02-27'),
  (12, 'Suresh Kumar',    'suresh@company.test',  40, 11,   'Accountant',           65000, '2020-10-12'),
  (13, 'Anjali Gupta',    'anjali@company.test',  50, 1,    'Support Lead',         90000, '2019-12-01'),
  (14, 'Nikhil Joshi',    'nikhil@company.test',  50, 13,   'Support Engineer',     55000, '2023-06-19'),
  (15, 'Pooja Shah',      NULL,                   NULL, 1,  'Intern',               25000, '2024-05-02');

-- ---------------------------------------------------------------- an online shop
CREATE TABLE customers (
  customer_id INTEGER PRIMARY KEY,
  name        TEXT NOT NULL,
  city        TEXT,
  country     TEXT,
  joined_on   TEXT
);
INSERT INTO customers VALUES
  (1, 'Aarav Sharma',  'Pune',      'India',     '2023-01-15'),
  (2, 'Diya Patel',    'Ahmedabad', 'India',     '2023-02-20'),
  (3, 'Liam Smith',    'London',    'UK',        '2023-03-05'),
  (4, 'Emma Brown',    'Manchester','UK',        '2023-04-11'),
  (5, 'Noah Wilson',   'New York',  'USA',       '2023-05-23'),
  (6, 'Mia Garcia',    'Austin',    'USA',       '2023-06-30'),
  (7, 'Ishaan Kapoor', 'Mumbai',    'India',     '2023-08-08'),
  (8, 'Sofia Rossi',   'Milan',     'Italy',     '2024-01-02');

CREATE TABLE products (
  product_id INTEGER PRIMARY KEY,
  name       TEXT NOT NULL,
  category   TEXT,
  price      REAL,
  stock      INTEGER
);
INSERT INTO products VALUES
  (1,  'Laptop',            'Electronics', 65000.00, 12),
  (2,  'Wireless Mouse',    'Electronics',   799.00, 150),
  (3,  'Mechanical Keyboard','Electronics', 3499.00, 60),
  (4,  'Monitor 24"',       'Electronics', 11999.00, 25),
  (5,  'Office Chair',      'Furniture',    8999.00, 18),
  (6,  'Standing Desk',     'Furniture',   21999.00, 7),
  (7,  'Notebook (pack)',   'Stationery',    249.00, 500),
  (8,  'Gel Pens (set)',    'Stationery',    199.00, 320),
  (9,  'Backpack',          'Accessories',  1999.00, 75),
  (10, 'Water Bottle',      'Accessories',   499.00, 0);

CREATE TABLE orders (
  order_id    INTEGER PRIMARY KEY,
  customer_id INTEGER REFERENCES customers(customer_id),
  order_date  TEXT,
  status      TEXT
);
INSERT INTO orders VALUES
  (101, 1, '2024-01-05', 'delivered'),
  (102, 2, '2024-01-07', 'delivered'),
  (103, 1, '2024-02-11', 'delivered'),
  (104, 3, '2024-02-14', 'cancelled'),
  (105, 5, '2024-03-01', 'delivered'),
  (106, 6, '2024-03-18', 'shipped'),
  (107, 7, '2024-04-02', 'delivered'),
  (108, 5, '2024-04-21', 'shipped'),
  (109, 4, '2024-05-09', 'pending'),
  (110, 1, '2024-05-30', 'pending'),
  (111, 7, '2024-06-12', 'delivered'),
  (112, 2, '2024-06-25', 'cancelled');

CREATE TABLE order_items (
  order_id   INTEGER REFERENCES orders(order_id),
  product_id INTEGER REFERENCES products(product_id),
  quantity   INTEGER,
  PRIMARY KEY (order_id, product_id)
);
INSERT INTO order_items VALUES
  (101, 1, 1), (101, 2, 1),
  (102, 7, 5), (102, 8, 2),
  (103, 3, 1), (103, 4, 2),
  (104, 6, 1),
  (105, 1, 2), (105, 9, 1),
  (106, 5, 1), (106, 2, 2),
  (107, 4, 1), (107, 3, 1), (107, 2, 1),
  (108, 10, 3),
  (109, 7, 10),
  (110, 9, 2), (110, 8, 4),
  (111, 1, 1),
  (112, 5, 2);

-- ---------------------------------------------------------------- a school
CREATE TABLE students (
  student_id INTEGER PRIMARY KEY,
  name       TEXT NOT NULL,
  age        INTEGER,
  city       TEXT
);
INSERT INTO students VALUES
  (1, 'Ananya', 19, 'Chennai'),
  (2, 'Bharat', 20, 'Delhi'),
  (3, 'Chitra', 19, 'Kochi'),
  (4, 'Deepak', 21, 'Delhi'),
  (5, 'Esha',   20, 'Pune'),
  (6, 'Farhan', 22, 'Mumbai'),
  (7, 'Gauri',  19, 'Chennai'),
  (8, 'Harsh',  21, 'Jaipur');

CREATE TABLE courses (
  course_id INTEGER PRIMARY KEY,
  title     TEXT NOT NULL,
  credits   INTEGER
);
INSERT INTO courses VALUES
  (1, 'Data Structures', 4),
  (2, 'Databases',       3),
  (3, 'Operating Systems', 4),
  (4, 'Computer Networks', 3),
  (5, 'Mathematics',     4);

CREATE TABLE enrollments (
  student_id INTEGER REFERENCES students(student_id),
  course_id  INTEGER REFERENCES courses(course_id),
  marks      INTEGER,
  PRIMARY KEY (student_id, course_id)
);
INSERT INTO enrollments VALUES
  (1, 1, 92), (1, 2, 85), (1, 5, 78),
  (2, 1, 67), (2, 3, 72),
  (3, 2, 88), (3, 4, 91),
  (4, 1, 55), (4, 2, 61), (4, 3, 58),
  (5, 5, 95), (5, 2, 79),
  (6, 3, 83),
  (7, 1, 74), (7, 4, 69);
-- student 8 (Harsh) is not enrolled in anything: handy for LEFT JOIN practice
